// CreatureCombatRewards - awards ArkShop points for killing wild creatures.
//
// A kill pays out when the creature falls under one of the creature types defined in config.json
// (for example "Aggressive" or "Threatening") and its level sits inside one of that type's level
// ranges. Creatures on the Excluded list, and unlisted creatures that the Fallback section does not
// cover, pay nothing.
//
// Everything here runs on the game thread (the death hook, AsaApi's one-second timer callback and
// the console/RCON commands), so the globals below need no locking.

#include "API/ARK/Ark.h"

#include <fstream>
#include <optional>
#include <string>

#include "RewardLogic.h"
#include "ShopBridge.h"

#ifndef PROJECT_NAME
#define PROJECT_NAME "CreatureCombatRewards"
#endif

namespace
{
	using namespace CombatRewards;

	const char* const kHookDie = "APrimalDinoCharacter.Die(float,FDamageEvent&,AController*,AActor*)";
	const char* const kReloadCommand = "CreatureCombatRewards.Reload";
	const char* const kTimerId = "CreatureCombatRewards.Payout";

	// Player and tribe teams start at 50000; anything below is a wild creature.
	constexpr int kFirstPlayerTeam = 50000;

	// How often (seconds) to repeat the "ArkShop is not available" warning while points are waiting.
	constexpr int kShopWarningInterval = 60;

	Settings g_settings;
	PendingPayouts g_pending;
	int g_seconds_since_payout = 0;
	int g_shop_warning_cooldown = 0;
	bool g_hook_installed = false;

	// ------------------------------------------------------------------------------------------
	// Text helpers
	// ------------------------------------------------------------------------------------------

	std::string ToUtf8(const FString& text)
	{
		return AsaApi::Tools::Utf8Encode(std::wstring(*text));
	}

	FString FromUtf8(const std::string& text)
	{
		return FString(AsaApi::Tools::Utf8Decode(text));
	}

	// ------------------------------------------------------------------------------------------
	// Config
	// ------------------------------------------------------------------------------------------

	Settings LoadSettings()
	{
		const std::string path = AsaApi::Tools::GetCurrentDir() + "/ArkApi/Plugins/" PROJECT_NAME "/config.json";

		std::ifstream file(path);
		if (!file.is_open())
			throw std::runtime_error("cannot open " + path);

		nlohmann::json root;
		try
		{
			// Last argument: allow // and /* */ comments in config.json.
			root = nlohmann::json::parse(file, nullptr, true, true);
		}
		catch (const nlohmann::json::exception& error)
		{
			throw std::runtime_error(std::string("config.json is not valid JSON: ") + error.what());
		}

		return ParseSettings(root);
	}

	bool ReloadSettings(std::string& error)
	{
		try
		{
			Settings loaded = LoadSettings();

			for (const std::string& warning : loaded.warnings)
				Log::GetLog()->warn("Config: {}", warning);

			Log::GetLog()->info("Config loaded: {} creature type(s), {} excluded entr(ies), payout every {}s, multiplier x{}",
				loaded.creature_types.size(), loaded.excluded.size(), loaded.payout_interval_seconds, loaded.points_multiplier);

			g_settings = std::move(loaded);
			return true;
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return false;
		}
	}

	// ------------------------------------------------------------------------------------------
	// Payouts
	// ------------------------------------------------------------------------------------------

	void FlushPayouts()
	{
		if (g_pending.Empty())
			return;

		const Shop::Api shop = Shop::Resolve();
		if (shop.status != Shop::Status::Ready)
		{
			if (g_shop_warning_cooldown <= 0)
			{
				g_shop_warning_cooldown = kShopWarningInterval;

				if (shop.status == Shop::Status::ShopNotLoaded)
					Log::GetLog()->warn("ArkShop is not loaded; {} point(s) are waiting to be paid", g_pending.TotalPoints());
				else
					Log::GetLog()->error("ArkShop is loaded but does not export {}; {} point(s) are waiting to be paid",
						Shop::kAddPointsSymbol, g_pending.TotalPoints());
			}
			return;
		}

		g_pending.Flush(
			[&shop](const std::string& eos_id, int points)
			{
				return shop.add_points(points, FString(eos_id));
			},
			[](const std::string& eos_id, int points)
			{
				Log::GetLog()->error("Gave up paying {} point(s) to {}: ArkShop refused the payout repeatedly", points, eos_id);
			});
	}

	void OnTimer()
	{
		if (g_shop_warning_cooldown > 0)
			--g_shop_warning_cooldown;

		const int interval = g_settings.payout_interval_seconds > 0 ? g_settings.payout_interval_seconds : 1;
		if (++g_seconds_since_payout < interval)
			return;

		g_seconds_since_payout = 0;
		FlushPayouts();
	}

	// ------------------------------------------------------------------------------------------
	// Kill handling
	// ------------------------------------------------------------------------------------------

	struct Recipient
	{
		AShooterPlayerController* controller = nullptr;
		std::string eos_id;
		bool via_tame = false;
	};

	struct PendingKill
	{
		Recipient recipient;
		CreatureInfo creature;
		Reward reward;
		std::string creature_name;
	};

	std::optional<Recipient> MakeRecipient(AShooterPlayerController* controller, bool via_tame)
	{
		if (controller == nullptr)
			return std::nullopt;

		Recipient recipient;
		recipient.controller = controller;
		recipient.eos_id = AsaApi::IApiUtils::GetEOSIDFromController(controller).ToString();
		recipient.via_tame = via_tame;

		if (recipient.eos_id.empty())
			return std::nullopt;

		return recipient;
	}

	AShooterPlayerController* FindNearestTeamMember(int team, const FVector& location, double max_distance)
	{
		UWorld* world = AsaApi::GetApiUtils().GetWorld();
		if (world == nullptr)
			return nullptr;

		AShooterPlayerController* nearest = nullptr;
		double nearest_distance = max_distance;

		const auto& controllers = world->PlayerControllerListField();
		for (TWeakObjectPtr<APlayerController> weak_controller : controllers)
		{
			auto* controller = static_cast<AShooterPlayerController*>(weak_controller.Get());
			if (controller == nullptr || controller->TargetingTeamField() != team)
				continue;

			if (AsaApi::IApiUtils::IsPlayerDead(controller))
				continue;

			const double distance = FVector::Distance(AsaApi::IApiUtils::GetPosition(controller), location);
			if (distance <= nearest_distance)
			{
				nearest = controller;
				nearest_distance = distance;
			}
		}

		return nearest;
	}

	/**
	 * \brief Works out which player (if any) should be credited with a kill.
	 */
	std::optional<Recipient> FindRecipient(APrimalDinoCharacter* victim, AController* killer, AActor* damage_causer)
	{
		// On foot or mounted, the killing blow is credited to the player's own controller.
		if (killer != nullptr && !killer->IsLocalController()
			&& killer->IsA(AShooterPlayerController::GetPrivateStaticClass()))
		{
			return MakeRecipient(static_cast<AShooterPlayerController*>(killer), false);
		}

		if (!g_settings.reward_unridden_tame_kills)
			return std::nullopt;

		// Otherwise: was it an unridden tame? Credit the closest living tribe member in range.
		AActor* source = damage_causer;
		if (killer != nullptr)
		{
			if (APawn* pawn = killer->PawnField().Get())
				source = pawn;
		}

		if (source == nullptr || !source->IsA(APrimalDinoCharacter::GetPrivateStaticClass()))
			return std::nullopt;

		const int team = source->TargetingTeamField();
		if (team < kFirstPlayerTeam)
			return std::nullopt;

		return MakeRecipient(FindNearestTeamMember(team, victim->GetLocation(), g_settings.tame_kill_max_distance), true);
	}

	CreatureInfo DescribeCreature(APrimalDinoCharacter* dino)
	{
		CreatureInfo info;

		// Walk up the blueprint inheritance chain: MegaRex_Character_BP_C -> Rex_Character_BP_C -> ...
		// and stop at the first native (C++) class, whose name has no "_C" suffix.
		int depth = 0;
		for (UStruct* current = dino->ClassPrivateField(); current != nullptr && depth < 16;
			current = current->SuperStructField(), ++depth)
		{
			std::string name = current->NameField().ToString().ToString();
			if (depth > 0 && !EndsWith(name, "_C"))
				break;

			info.class_chain.push_back(std::move(name));
		}

		info.name_tag = dino->DinoNameTagField().ToString().ToString();
		if (info.name_tag == "None")
			info.name_tag.clear();

		info.blueprint = AsaApi::IApiUtils::GetBlueprint(dino).ToString();
		info.is_carnivore = dino->bIsCarnivore()();

		if (UPrimalCharacterStatusComponent* status = dino->MyCharacterStatusComponentField())
			info.level = status->BaseCharacterLevelField() + status->ExtraCharacterLevelField();

		return info;
	}

	/**
	 * \brief Called before the game processes the death. Returns the details of a kill that could
	 * pay out, or nothing if this death is not a player killing a wild creature.
	 */
	std::optional<PendingKill> PrepareKill(APrimalDinoCharacter* victim, AController* killer, AActor* damage_causer)
	{
		if (victim == nullptr || (killer == nullptr && damage_causer == nullptr))
			return std::nullopt;

		// Only wild creatures pay out; tamed and claimed ones never do.
		if (victim->TargetingTeamField() >= kFirstPlayerTeam)
			return std::nullopt;

		// Already a corpse (the game can call Die more than once). bIsDead is only set inside Die,
		// so it is still false for the death that counts.
		if (victim->bIsDead()())
			return std::nullopt;

		std::optional<Recipient> recipient = FindRecipient(victim, killer, damage_causer);
		if (!recipient)
			return std::nullopt;

		PendingKill kill;
		kill.recipient = std::move(*recipient);
		kill.creature = DescribeCreature(victim);
		kill.reward = Evaluate(g_settings, kill.creature);
		kill.creature_name = ToUtf8(victim->DescriptiveNameField());
		return kill;
	}

	const std::string& TypeName(const Reward& reward)
	{
		static const std::string none = "-";

		const int index = reward.classification.type_index;
		if (index < 0 || index >= static_cast<int>(g_settings.creature_types.size()))
			return none;

		return g_settings.creature_types[static_cast<size_t>(index)].name;
	}

	void LogKill(const PendingKill& kill)
	{
		const std::string own_class = kill.creature.class_chain.empty() ? std::string("?") : kill.creature.class_chain.front();
		const std::string parent_class = kill.creature.class_chain.size() > 1 ? kill.creature.class_chain[1] : std::string("-");

		Log::GetLog()->info(
			"Kill by {}{}: \"{}\" class={} parent={} tag={} level={} diet={} -> type={} ({}{}{}) points={}",
			kill.recipient.eos_id,
			kill.recipient.via_tame ? " (tame)" : "",
			kill.creature_name,
			own_class,
			parent_class,
			kill.creature.name_tag.empty() ? std::string("-") : kill.creature.name_tag,
			kill.creature.level,
			kill.creature.is_carnivore ? "carnivore" : "herbivore",
			TypeName(kill.reward),
			MatchKindName(kill.reward.classification.kind),
			kill.reward.classification.matched_entry.empty() ? "" : ": ",
			kill.reward.classification.matched_entry,
			kill.reward.points);
	}

	void Notify(const PendingKill& kill)
	{
		const NotificationSettings& notify = g_settings.notifications;
		if (!notify.enabled || kill.recipient.controller == nullptr)
			return;

		const std::string text = FormatTemplate(notify.message, {
			{ "points", std::to_string(kill.reward.points) },
			{ "creature", kill.creature_name },
			{ "level", std::to_string(kill.creature.level) },
			{ "type", TypeName(kill.reward) } });

		// The finished text is passed as an argument, never as the format string, so braces in
		// creature names or in the configured message cannot upset the formatter.
		const std::wstring wide_text = AsaApi::Tools::Utf8Decode(text);

		if (notify.use_chat)
		{
			AsaApi::GetApiUtils().SendChatMessage(kill.recipient.controller, FromUtf8(notify.sender), L"{}", wide_text.c_str());
		}
		else
		{
			AsaApi::GetApiUtils().SendNotification(kill.recipient.controller, FColorList::Green, notify.text_scale,
				notify.display_time, nullptr, L"{}", wide_text.c_str());
		}
	}

	/**
	 * \brief Called once the game has confirmed the creature really died.
	 */
	void CommitKill(const PendingKill& kill)
	{
		if (g_settings.log_kills)
			LogKill(kill);

		if (kill.reward.points <= 0)
			return;

		g_pending.Add(kill.recipient.eos_id, kill.reward.points);
		Notify(kill);

		if (g_settings.payout_interval_seconds == 0)
			FlushPayouts();
	}

	// ------------------------------------------------------------------------------------------
	// Hook
	// ------------------------------------------------------------------------------------------

	DECLARE_HOOK(APrimalDinoCharacter_Die, bool, APrimalDinoCharacter*, float, FDamageEvent*, AController*, AActor*);

	bool Hook_APrimalDinoCharacter_Die(APrimalDinoCharacter* _this, float KillingDamage, FDamageEvent* DamageEvent,
		AController* Killer, AActor* DamageCauser)
	{
		std::optional<PendingKill> kill;

		try
		{
			kill = PrepareKill(_this, Killer, DamageCauser);
		}
		catch (const std::exception& exception)
		{
			Log::GetLog()->error("Kill check failed: {}", exception.what());
		}

		const bool died = APrimalDinoCharacter_Die_original(_this, KillingDamage, DamageEvent, Killer, DamageCauser);

		if (died && kill)
		{
			try
			{
				CommitKill(*kill);
			}
			catch (const std::exception& exception)
			{
				Log::GetLog()->error("Kill reward failed: {}", exception.what());
			}
		}

		return died;
	}

	// ------------------------------------------------------------------------------------------
	// Commands
	// ------------------------------------------------------------------------------------------

	void ReloadConsole(APlayerController* player_controller, FString* /*command*/, bool /*write_to_log*/)
	{
		auto* shooter_controller = static_cast<AShooterPlayerController*>(player_controller);
		if (shooter_controller == nullptr || !shooter_controller->bIsAdmin()())
			return;

		std::string error;
		if (ReloadSettings(error))
		{
			AsaApi::GetApiUtils().SendServerMessage(shooter_controller, FColorList::Green, "CreatureCombatRewards: config reloaded");
		}
		else
		{
			Log::GetLog()->error("Config reload failed: {}", error);
			AsaApi::GetApiUtils().SendServerMessage(shooter_controller, FColorList::Red, "CreatureCombatRewards: reload failed - {}", error);
		}
	}

	void ReloadRcon(RCONClientConnection* rcon_connection, RCONPacket* rcon_packet, UWorld* /*world*/)
	{
		FString reply;

		std::string error;
		if (ReloadSettings(error))
		{
			reply = "CreatureCombatRewards: config reloaded";
		}
		else
		{
			Log::GetLog()->error("Config reload failed: {}", error);
			reply = FString("CreatureCombatRewards: reload failed - " + error);
		}

		rcon_connection->SendMessageW(rcon_packet->Id, 0, &reply);
	}
} // namespace

// Called by AsaApi when the plugin is loaded.
extern "C" __declspec(dllexport) void Plugin_Init()
{
	Log::Get().Init(PROJECT_NAME);

	std::string error;
	if (!ReloadSettings(error))
	{
		Log::GetLog()->error("Config not loaded: {}", error);
		Log::GetLog()->error("No kills will pay out until config.json is fixed and {} is run", kReloadCommand);
	}

	g_hook_installed = AsaApi::GetHooks().SetHook(kHookDie, reinterpret_cast<LPVOID>(&Hook_APrimalDinoCharacter_Die),
		&APrimalDinoCharacter_Die_original);
	if (!g_hook_installed)
		Log::GetLog()->error("Could not hook {}; kills cannot be detected", kHookDie);

	AsaApi::GetCommands().AddConsoleCommand(kReloadCommand, &ReloadConsole);
	AsaApi::GetCommands().AddRconCommand(kReloadCommand, &ReloadRcon);
	AsaApi::GetCommands().AddOnTimerCallback(kTimerId, &OnTimer);

	if (Shop::Resolve().status == Shop::Status::SymbolNotFound)
		Log::GetLog()->error("ArkShop is loaded but does not export {}; points cannot be paid", Shop::kAddPointsSymbol);
}

// Called by AsaApi when the plugin is unloaded.
extern "C" __declspec(dllexport) void Plugin_Unload()
{
	AsaApi::GetCommands().RemoveOnTimerCallback(kTimerId);
	AsaApi::GetCommands().RemoveConsoleCommand(kReloadCommand);
	AsaApi::GetCommands().RemoveRconCommand(kReloadCommand);

	if (g_hook_installed)
	{
		AsaApi::GetHooks().DisableHook(kHookDie, reinterpret_cast<LPVOID>(&Hook_APrimalDinoCharacter_Die));
		g_hook_installed = false;
	}

	// Pay out whatever is still queued so a plugin reload does not cost players their points.
	g_shop_warning_cooldown = 0;
	FlushPayouts();

	if (!g_pending.Empty())
		Log::GetLog()->warn("{} point(s) were still unpaid when the plugin unloaded", g_pending.TotalPoints());
}
