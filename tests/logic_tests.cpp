// Stand-alone tests for src/RewardLogic.h. No game, AsaApi DLL or Windows needed.
//
// Build and run from the project folder (any C++20 compiler):
//   cl /std:c++20 /EHsc /I extern\AsaApi\AsaApi\Core\Public /I src tests\logic_tests.cpp && logic_tests.exe configs\config.json
//   clang++ -std=c++20 -I extern/AsaApi/AsaApi/Core/Public -I src tests/logic_tests.cpp -o logic_tests && ./logic_tests configs/config.json
//
// The optional argument is a config.json to validate. Add --shipped after it to also check the file
// against the expectations for the shipped config at the bottom of this file:
//   logic_tests.exe configs\config.json --shipped
// configs\CommentedConfig.json holds the same values, so it must pass the same check.

#include <fstream>
#include <iostream>
#include <string>

#include "RewardLogic.h"

using namespace CombatRewards;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(condition) \
	do { \
		++g_checks; \
		if (!(condition)) { \
			++g_failures; \
			std::cerr << "FAILED line " << __LINE__ << ": " #condition "\n"; \
		} \
	} while (false)

#define CHECK_THROWS(expression) \
	do { \
		++g_checks; \
		bool threw = false; \
		try { (void)(expression); } catch (const std::exception&) { threw = true; } \
		if (!threw) { \
			++g_failures; \
			std::cerr << "FAILED line " << __LINE__ << ": expected an error from " #expression "\n"; \
		} \
	} while (false)

static CreatureInfo Creature(std::vector<std::string> chain, std::string tag, bool carnivore, int level,
	std::string blueprint = "")
{
	CreatureInfo info;
	info.class_chain = std::move(chain);
	info.name_tag = std::move(tag);
	info.is_carnivore = carnivore;
	info.level = level;
	info.blueprint = std::move(blueprint);
	return info;
}

static Settings Parse(const char* text)
{
	return ParseSettings(nlohmann::json::parse(text, nullptr, true, true));
}

static const char* kBaseConfig = R"({
	// comments are allowed
	"General": { "PayoutIntervalSeconds": 10, "PointsMultiplier": 1.0 },
	"Fallback": { "Carnivores": "Aggressive", "Herbivores": "" },
	"CreatureTypes": [
		{
			"Name": "Threatening",
			"Creatures": [ "MegaRex_Character_BP", "Rex_Character_BP_C", "Xenomorph_Character_BP*", "/Game/Mods/Scary/*" ],
			"LevelRanges": [
				{ "MinLevel": 1, "MaxLevel": 99, "Points": 10 },
				{ "MinLevel": 100, "Points": 20 }
			]
		},
		{
			"Name": "Aggressive",
			"Creatures": [ "*_Character_BP_Corrupt", "Chalico_Character_BP", "Blueprint'/Game/PrimalEarth/Dinos/Bigfoot/Bigfoot_Character_BP.Bigfoot_Character_BP'" ],
			"LevelRanges": [
				{ "MinLevel": 1, "MaxLevel": 49, "Points": 1 },
				{ "MinLevel": 50, "MaxLevel": 150, "Points": 3 }
			]
		}
	],
	"Excluded": [ "Coel_Character_BP", "Ptero_Character_BP", "Bee" ]
})";

static void TestStrings()
{
	CHECK(WildcardMatch("*", ""));
	CHECK(WildcardMatch("*", "anything"));
	CHECK(WildcardMatch("rex*", "rex_character_bp"));
	CHECK(WildcardMatch("*_corrupt", "trike_character_bp_corrupt"));
	CHECK(WildcardMatch("*rex*", "megarex_character_bp"));
	CHECK(WildcardMatch("r?x", "rex"));
	CHECK(!WildcardMatch("r?x", "reex"));
	CHECK(!WildcardMatch("rex", "rex_character_bp"));
	CHECK(!WildcardMatch("*_corrupt", "trike_character_bp"));
	CHECK(WildcardMatch("a*b*c", "aXXbYYc"));
	CHECK(!WildcardMatch("a*b*c", "aXXbYY"));
	CHECK(!WildcardMatch("", "x"));
	CHECK(WildcardMatch("", ""));

	CHECK(NormalizeClassName("Rex_Character_BP_C") == "rex_character_bp");
	CHECK(NormalizeClassName("  Rex_Character_BP ") == "rex_character_bp");
	CHECK(NormalizeClassName("Leech_Character_C") == "leech_character");

	const std::string expected = "/game/primalearth/dinos/rex/rex_character_bp";
	CHECK(NormalizeBlueprintPath("Blueprint'/Game/PrimalEarth/Dinos/Rex/Rex_Character_BP.Rex_Character_BP'") == expected);
	CHECK(NormalizeBlueprintPath("/Game/PrimalEarth/Dinos/Rex/Rex_Character_BP.Rex_Character_BP_C") == expected);
	CHECK(NormalizeBlueprintPath("/Game/PrimalEarth/Dinos/Rex/Rex_Character_BP") == expected);
	CHECK(NormalizeBlueprintPath("/Game/PrimalEarth/Dinos/Rex/Rex_Character_BP_C") == expected);
	CHECK(NormalizeBlueprintPath("") == "");

	CHECK(FormatTemplate("+{points} for {creature} ({level}) {points}", { {"points", "5"}, {"creature", "Rex"}, {"level", "120"} })
		== "+5 for Rex (120) 5");
	CHECK(FormatTemplate("{unknown} {points}", { {"points", "5"} }) == "{unknown} 5");
	// A value that itself looks like a placeholder must not be expanded again.
	CHECK(FormatTemplate("{creature}", { {"creature", "{creature}!"}, {"points", "5"} }) == "{creature}!");
}

static void TestClassification()
{
	const Settings settings = Parse(kBaseConfig);
	CHECK(settings.creature_types.size() == 2);
	CHECK(settings.payout_interval_seconds == 10);
	CHECK(settings.fallback_carnivore_type == 1);
	CHECK(settings.fallback_herbivore_type == -1);
	CHECK(settings.warnings.empty());

	// Exact class match, with and without _C in the config.
	auto rex = Creature({ "Rex_Character_BP_C", "Dino_Character_BP_C" }, "Rex", true, 120);
	Reward reward = Evaluate(settings, rex);
	CHECK(reward.classification.kind == MatchKind::Listed);
	CHECK(reward.classification.type_index == 0);
	CHECK(reward.points == 20);

	rex.level = 99;
	CHECK(Evaluate(settings, rex).points == 10);
	rex.level = 100;
	CHECK(Evaluate(settings, rex).points == 20);
	rex.level = 5000;
	CHECK(Evaluate(settings, rex).points == 20);
	rex.level = 0;
	CHECK(Evaluate(settings, rex).points == 0); // below every range

	// A child blueprint inherits its parent's type (Tek Rex -> Rex).
	const auto tek_rex = Creature({ "BionicRex_Character_BP_C", "Rex_Character_BP_C", "Dino_Character_BP_C" }, "Rex", true, 50);
	reward = Evaluate(settings, tek_rex);
	CHECK(reward.classification.type_index == 0);
	CHECK(reward.points == 10);

	// Unlisted carnivore -> fallback type.
	const auto raptor = Creature({ "Raptor_Character_BP_C", "Dino_Character_BP_C" }, "Raptor", true, 60);
	reward = Evaluate(settings, raptor);
	CHECK(reward.classification.kind == MatchKind::Fallback);
	CHECK(reward.classification.type_index == 1);
	CHECK(reward.points == 3);

	// Aggressive range stops at 150: above it there is no reward.
	auto high_raptor = raptor;
	high_raptor.level = 151;
	CHECK(Evaluate(settings, high_raptor).points == 0);

	// Unlisted herbivore -> nothing.
	const auto trike = Creature({ "Trike_Character_BP_C", "Dino_Character_BP_C" }, "Trike", false, 150);
	reward = Evaluate(settings, trike);
	CHECK(reward.classification.kind == MatchKind::None);
	CHECK(reward.points == 0);

	// Listed herbivore pays despite the diet.
	const auto chalico = Creature({ "Chalico_Character_BP_C", "Dino_Character_BP_C" }, "Chalicotherium", false, 10);
	reward = Evaluate(settings, chalico);
	CHECK(reward.classification.kind == MatchKind::Listed);
	CHECK(reward.points == 1);

	// Excluded carnivore pays nothing even though the fallback would cover it.
	const auto coel = Creature({ "Coel_Character_BP_C", "Dino_Character_BP_C" }, "Coel", true, 150);
	reward = Evaluate(settings, coel);
	CHECK(reward.classification.kind == MatchKind::Excluded);
	CHECK(reward.points == 0);

	// Exclusion is inherited by child blueprints (Aberrant Coelacanth).
	const auto aberrant_coel = Creature({ "Coel_Character_BP_Aberrant_C", "Coel_Character_BP_C", "Dino_Character_BP_C" }, "Coel", true, 150);
	CHECK(Evaluate(settings, aberrant_coel).classification.kind == MatchKind::Excluded);

	// Excluded by name tag.
	const auto bee = Creature({ "Bee_Character_BP_C", "Dino_Character_BP_C" }, "Bee", true, 30);
	CHECK(Evaluate(settings, bee).classification.kind == MatchKind::Excluded);

	// The most specific class wins: a corrupted Pteranodon matches the Aggressive wildcard on its own
	// class before the Excluded entry on its parent class is considered.
	const auto corrupt_ptero = Creature({ "Ptero_Character_BP_Corrupt_C", "Ptero_Character_BP_C", "Dino_Character_BP_C" }, "Ptera", true, 60);
	reward = Evaluate(settings, corrupt_ptero);
	CHECK(reward.classification.kind == MatchKind::Listed);
	CHECK(reward.classification.type_index == 1);
	CHECK(reward.points == 3);

	// ...and a corrupted Rex is Aggressive (own class) rather than Threatening (parent class).
	const auto corrupt_rex = Creature({ "Rex_Character_BP_Corrupt_C", "Rex_Character_BP_C", "Dino_Character_BP_C" }, "Rex", true, 60);
	CHECK(Evaluate(settings, corrupt_rex).classification.type_index == 1);

	// Alpha listed explicitly.
	const auto alpha_rex = Creature({ "MegaRex_Character_BP_C", "Rex_Character_BP_C", "Dino_Character_BP_C" }, "Elite Rex", true, 30);
	reward = Evaluate(settings, alpha_rex);
	CHECK(reward.classification.type_index == 0);
	CHECK(reward.classification.matched_entry == "MegaRex_Character_BP");

	// Wildcard on class name.
	const auto reaper = Creature({ "Xenomorph_Character_BP_Male_C", "Xenomorph_Character_BP_C", "Dino_Character_BP_C" }, "Xenomorph", true, 200);
	CHECK(Evaluate(settings, reaper).classification.type_index == 0);

	// Blueprint path entries: exact (any spelling) and wildcard.
	const auto bigfoot = Creature({ "Bigfoot_Character_BP_C" }, "Bigfoot", false, 80,
		"Blueprint'/Game/PrimalEarth/Dinos/Bigfoot/Bigfoot_Character_BP.Bigfoot_Character_BP'");
	CHECK(Evaluate(settings, bigfoot).classification.type_index == 1);

	const auto modded = Creature({ "Horror_Character_BP_C", "Dino_Character_BP_C" }, "", false, 80,
		"Blueprint'/Game/Mods/Scary/Dinos/Horror_Character_BP.Horror_Character_BP'");
	reward = Evaluate(settings, modded);
	CHECK(reward.classification.kind == MatchKind::Listed);
	CHECK(reward.classification.type_index == 0);

	// A path entry never matches on class name alone.
	const auto other_mod = Creature({ "Horror_Character_BP_C" }, "", false, 80,
		"Blueprint'/Game/Mods/Other/Horror_Character_BP.Horror_Character_BP'");
	CHECK(Evaluate(settings, other_mod).classification.kind == MatchKind::None);

	// Matching ignores case.
	const auto shouty = Creature({ "REX_CHARACTER_BP_C" }, "", false, 10);
	CHECK(Evaluate(settings, shouty).classification.type_index == 0);

	// Nothing known about the creature at all: no crash, no reward.
	const CreatureInfo empty;
	CHECK(Evaluate(settings, empty).points == 0);
}

static void TestMultiplier()
{
	CHECK(ApplyMultiplier(10, 1.0) == 10);
	CHECK(ApplyMultiplier(10, 2.0) == 20);
	CHECK(ApplyMultiplier(3, 1.5) == 5);   // 4.5 rounds up
	CHECK(ApplyMultiplier(1, 0.4) == 0);
	CHECK(ApplyMultiplier(1, 0.5) == 1);
	CHECK(ApplyMultiplier(0, 5.0) == 0);
	CHECK(ApplyMultiplier(10, 0.0) == 0);
	CHECK(ApplyMultiplier(2000000000, 5.0) == kIntMax);

	Settings settings = Parse(kBaseConfig);
	settings.points_multiplier = 2.0;
	const auto rex = Creature({ "Rex_Character_BP_C" }, "Rex", true, 120);
	CHECK(Evaluate(settings, rex).points == 40);
}

static void TestConfigErrors()
{
	// Empty config is usable but pays nothing, and says so.
	const Settings empty = Parse("{}");
	CHECK(empty.creature_types.empty());
	CHECK(!empty.warnings.empty());
	CHECK(empty.payout_interval_seconds == 30);
	CHECK(empty.notifications.enabled);

	CHECK_THROWS(Parse("[]"));
	CHECK_THROWS(Parse(R"({ "CreatureTypes": {} })"));
	CHECK_THROWS(Parse(R"({ "CreatureTypes": [ { "Creatures": [] } ] })"));                              // no Name
	CHECK_THROWS(Parse(R"({ "CreatureTypes": [ { "Name": "A" }, { "Name": "a" } ] })"));                  // duplicate
	CHECK_THROWS(Parse(R"({ "CreatureTypes": [ { "Name": "A", "Creatures": [ 5 ] } ] })"));               // not text
	CHECK_THROWS(Parse(R"({ "CreatureTypes": [ { "Name": "A", "LevelRanges": [ { "MinLevel": 1 } ] } ] })"));                         // no Points
	CHECK_THROWS(Parse(R"({ "CreatureTypes": [ { "Name": "A", "LevelRanges": [ { "MinLevel": 9, "MaxLevel": 3, "Points": 1 } ] } ] })"));
	CHECK_THROWS(Parse(R"({ "CreatureTypes": [ { "Name": "A", "LevelRanges": [ { "Points": -1 } ] } ] })"));
	CHECK_THROWS(Parse(R"({ "CreatureTypes": [ { "Name": "A", "LevelRanges": [ { "Points": 1.5 } ] } ] })"));
	CHECK_THROWS(Parse(R"({ "CreatureTypes": [ { "Name": "A", "LevelRanges": [ { "Points": "1" } ] } ] })"));
	CHECK_THROWS(Parse(R"({ "Fallback": { "Carnivores": "Nope" } })"));
	CHECK_THROWS(Parse(R"({ "General": { "PayoutIntervalSeconds": -1 } })"));
	CHECK_THROWS(Parse(R"({ "General": { "PointsMultiplier": -2 } })"));
	CHECK_THROWS(Parse(R"({ "General": { "PointsMultiplier": "big" } })"));
	CHECK_THROWS(Parse(R"({ "General": { "LogKills": "yes" } })"));
	CHECK_THROWS(Parse(R"({ "Excluded": "Dodo_Character_BP" })"));
	CHECK_THROWS(Parse(R"({ "Notifications": [] })"));

	// The error names the place that is wrong.
	try
	{
		Parse(R"({ "CreatureTypes": [ { "Name": "Apex", "LevelRanges": [ { "MinLevel": 9, "MaxLevel": 3, "Points": 1 } ] } ] })");
	}
	catch (const std::exception& error)
	{
		const std::string message = error.what();
		CHECK(message.find("Apex") != std::string::npos);
		CHECK(message.find("LevelRanges[0]") != std::string::npos);
	}

	// Overlapping ranges are accepted with a warning; the first one wins.
	const Settings overlapping = Parse(R"({ "CreatureTypes": [ { "Name": "A", "Creatures": [ "X" ],
		"LevelRanges": [ { "MinLevel": 1, "MaxLevel": 50, "Points": 1 }, { "MinLevel": 40, "MaxLevel": 90, "Points": 2 } ] } ] })");
	CHECK(overlapping.warnings.size() == 1);
	CHECK(BasePointsForLevel(overlapping.creature_types[0], 45) == 1);
	CHECK(BasePointsForLevel(overlapping.creature_types[0], 60) == 2);

	// Fallback type names ignore case; MaxLevel 0 means open-ended; a multiplier of 0 is allowed.
	const Settings loose = Parse(R"({ "General": { "PointsMultiplier": 0, "PayoutIntervalSeconds": 0 },
		"Fallback": { "Carnivores": "aggressive", "Herbivores": "AGGRESSIVE" },
		"CreatureTypes": [ { "Name": "Aggressive", "LevelRanges": [ { "MinLevel": 10, "MaxLevel": 0, "Points": 4 } ] } ] })");
	CHECK(loose.fallback_carnivore_type == 0);
	CHECK(loose.fallback_herbivore_type == 0);
	CHECK(BasePointsForLevel(loose.creature_types[0], 9) == 0);
	CHECK(BasePointsForLevel(loose.creature_types[0], 100000) == 4);
	CHECK(Evaluate(loose, Creature({ "Trike_Character_BP_C" }, "Trike", false, 50)).points == 0); // multiplier 0
}

static void TestPendingPayouts()
{
	PendingPayouts pending;
	CHECK(pending.Empty());

	pending.Add("aaa", 5);
	pending.Add("aaa", 7);
	pending.Add("bbb", 1);
	pending.Add("", 99);     // ignored
	pending.Add("ccc", 0);   // ignored
	pending.Add("ccc", -4);  // ignored
	CHECK(pending.TotalPoints() == 13);

	// One player's payout fails; the other goes through and the failure stays queued.
	int paid_a = 0, paid_b = 0, dropped = 0;
	const auto pay_only_b = [&](const std::string& eos_id, int points)
	{
		if (eos_id == "bbb") { paid_b += points; return true; }
		return false;
	};
	const auto on_dropped = [&](const std::string&, int points) { dropped += points; };

	pending.Flush(pay_only_b, on_dropped);
	CHECK(paid_b == 1);
	CHECK(pending.TotalPoints() == 12);
	CHECK(dropped == 0);

	// Points earned while a payout is failing are added to the same entry.
	pending.Add("aaa", 3);
	pending.Flush(pay_only_b, on_dropped);
	CHECK(pending.TotalPoints() == 15);
	CHECK(dropped == 0);

	// Third failure: given up on, reported once, and removed.
	pending.Flush(pay_only_b, on_dropped);
	CHECK(dropped == 15);
	CHECK(pending.Empty());

	// Normal path: everything is paid exactly once.
	pending.Add("aaa", 4);
	pending.Add("aaa", 6);
	pending.Flush([&](const std::string&, int points) { paid_a += points; return true; }, on_dropped);
	CHECK(paid_a == 10);
	CHECK(pending.Empty());

	pending.Flush([&](const std::string&, int points) { paid_a += points; return true; }, on_dropped);
	CHECK(paid_a == 10);
}

// Checks the config.json that ships with the plugin (or whichever file is passed in).
static void TestConfigFile(const std::string& path, bool shipped_expectations)
{
	std::ifstream file(path);
	if (!file.is_open())
	{
		++g_failures;
		std::cerr << "FAILED: cannot open " << path << "\n";
		return;
	}

	Settings settings;
	try
	{
		settings = ParseSettings(nlohmann::json::parse(file, nullptr, true, true));
	}
	catch (const std::exception& error)
	{
		++g_failures;
		std::cerr << "FAILED: " << path << ": " << error.what() << "\n";
		return;
	}

	std::cout << path << ": " << settings.creature_types.size() << " creature type(s), "
		<< settings.excluded.size() << " excluded entries\n";
	for (const auto& warning : settings.warnings)
		std::cout << "  warning: " << warning << "\n";

	if (!shipped_expectations)
		return;

	CHECK(settings.warnings.empty());
	CHECK(settings.creature_types.size() == 4);
	CHECK(settings.excluded.size() == 30);
	CHECK(settings.fallback_carnivore_type == 1);  // Aggressive
	CHECK(settings.fallback_herbivore_type == -1); // no reward

	// The shipped level ranges are built for a wild level cap of 300: twelve 30-level steps, the last
	// of which (330 and above) also catches tek, cave and OSD creatures that spawn over the cap.
	struct Expectation { CreatureInfo creature; const char* type; int points; };
	const Expectation expectations[] = {
		// Threatening: 3 to 30.
		{ Creature({ "Rex_Character_BP_C", "Dino_Character_BP_C" }, "Rex", true, 145), "Threatening", 13 },
		{ Creature({ "MegaRaptor_Character_BP_C", "Raptor_Character_BP_C", "Dino_Character_BP_C" }, "Elite Raptor", true, 12), "Threatening", 3 },
		{ Creature({ "Gigant_Character_BP_C", "Dino_Character_BP_C" }, "Gigant", true, 150), "Threatening", 15 },
		{ Creature({ "Gigant_Character_BP_C", "Dino_Character_BP_C" }, "Gigant", true, 299), "Threatening", 25 },
		{ Creature({ "Gigant_Character_BP_C", "Dino_Character_BP_C" }, "Gigant", true, 300), "Threatening", 28 },
		{ Creature({ "Gigant_Character_BP_C", "Dino_Character_BP_C" }, "Gigant", true, 329), "Threatening", 28 },
		{ Creature({ "Gigant_Character_BP_C", "Dino_Character_BP_C" }, "Gigant", true, 330), "Threatening", 30 },
		{ Creature({ "Wyvern_Character_BP_Fire_C", "Wyvern_Character_BP_Base_C", "Dino_Character_BP_C" }, "Wyvern", true, 190), "Threatening", 18 },
		{ Creature({ "Wyvern_Character_BP_Fire_C", "Wyvern_Character_BP_Base_C", "Dino_Character_BP_C" }, "Wyvern", true, 380), "Threatening", 30 },
		{ Creature({ "Xenomorph_Character_BP_Female_C", "Dino_Character_BP_C" }, "Xenomorph", true, 75), "Threatening", 8 },
		{ Creature({ "Rex_Character_BP_C", "Dino_Character_BP_C" }, "Rex", true, 650), "Threatening", 30 }, // cave or OSD level
		// Herbivores listed as Threatening pay despite the diet.
		{ Creature({ "Therizino_Character_BP_C", "Dino_Character_BP_C" }, "Therizinosaurus", false, 300), "Threatening", 28 },
		{ Creature({ "Dreadnoughtus_Character_BP_C", "Dino_Character_BP_C" }, "Dreadnoughtus", false, 250), "Threatening", 23 },

		// Aggressive: 1 to 6. Unlisted carnivores land here through the fallback.
		{ Creature({ "Raptor_Character_BP_C", "Dino_Character_BP_C" }, "Raptor", true, 35), "Aggressive", 1 },
		{ Creature({ "Carno_Character_BP_C", "Dino_Character_BP_C" }, "Carno", true, 150), "Aggressive", 3 },
		{ Creature({ "Piranha_Character_BP_C", "Dino_Character_BP_C" }, "Piranha", true, 5), "Aggressive", 1 },
		{ Creature({ "Direwolf_Character_BP_C", "Dino_Character_BP_Pack_C" }, "Direwolf", true, 320), "Aggressive", 6 },
		{ Creature({ "Chalico_Character_BP_C", "Dino_Character_BP_C" }, "Chalicotherium", false, 120), "Aggressive", 3 },
		{ Creature({ "Megatherium_Character_BP_C", "Dino_Character_BP_C" }, "Megatherium", false, 200), "Aggressive", 4 },
		// A Yeti is built on the Gigantopithecus blueprint, and a level over the cap pays no more than the cap.
		{ Creature({ "Yeti_Character_BP_C", "Bigfoot_Character_BP_C" }, "Bigfoot", false, 500), "Aggressive", 6 },
		// A corrupted Trike matches the Aggressive wildcard on its own class before its parent's Defensive entry.
		{ Creature({ "Trike_Character_BP_Corrupt_C", "Trike_Character_BP_C", "Dino_Character_BP_C" }, "Trike", false, 95), "Aggressive", 2 },

		// Defensive: 1 to 3. Herbivores that fight back when hit.
		{ Creature({ "Trike_Character_BP_C", "Dino_Character_BP_C" }, "Trike", false, 150), "Defensive", 2 },
		{ Creature({ "Stego_Character_BP_C", "Dino_Character_BP_C" }, "Stego", false, 40), "Defensive", 1 },
		{ Creature({ "BionicStego_Character_BP_C", "Stego_Character_BP_C", "Dino_Character_BP_C" }, "Stego", false, 360), "Defensive", 3 },
		{ Creature({ "Sauropod_Character_BP_C", "Dino_Character_BP_C" }, "Sauropod", false, 300), "Defensive", 3 },
		{ Creature({ "Iguanodon_Character_BP_C", "Dino_Character_BP_C" }, "Iguanodon", false, 50), "Defensive", 1 },
		{ Creature({ "Beaver_Character_BP_C", "Dino_Character_BP_C" }, "Beaver", false, 130), "Defensive", 2 },

		// Colossal: one flat payout, because a wild Titanosaur's level is always very low.
		{ Creature({ "Titanosaur_Character_BP_C", "Dino_Character_BP_C" }, "Titanosaur", false, 3), "Colossal", 50 },
		{ Creature({ "Titanosaur_Character_BP_C", "Dino_Character_BP_C" }, "Titanosaur", false, 300), "Colossal", 50 },

		// Unlisted herbivores pay nothing.
		{ Creature({ "Para_Character_BP_C", "Dino_Character_BP_C" }, "Para", false, 150), "", 0 },
		{ Creature({ "Moschops_Character_BP_C", "Dino_Character_BP_C" }, "Moschops", false, 190), "", 0 },
		{ Creature({ "Diplodocus_Character_BP_C", "Dino_Character_BP_C" }, "Diplodocus", false, 100), "", 0 },

		// Excluded creatures pay nothing, including carnivores the fallback would otherwise cover.
		{ Creature({ "Coel_Character_BP_C", "Dino_Character_BP_C" }, "Coel", true, 150), "", 0 },
		{ Creature({ "Salmon_Character_BP_C", "Dino_Character_BP_C" }, "Salmon", true, 150), "", 0 },
		{ Creature({ "Ptero_Character_BP_C", "Dino_Character_BP_C" }, "Ptera", true, 150), "", 0 },
		{ Creature({ "Hesperornis_Character_BP_C", "Dino_Character_BP_C" }, "Hesperornis", true, 50), "", 0 },
		{ Creature({ "Dodo_Character_BP_C", "Dino_Character_BP_C" }, "Dodo", false, 150), "", 0 },
		{ Creature({ "Bee_Character_BP_C", "Dino_Character_BP_C" }, "Bee", true, 150), "", 0 },
	};

	for (const auto& expectation : expectations)
	{
		const Reward reward = Evaluate(settings, expectation.creature);
		const std::string type = reward.classification.type_index >= 0
			? settings.creature_types[static_cast<size_t>(reward.classification.type_index)].name
			: std::string();

		++g_checks;
		if (type != expectation.type || reward.points != expectation.points)
		{
			++g_failures;
			std::cerr << "FAILED: " << expectation.creature.class_chain.front() << " level " << expectation.creature.level
				<< ": expected \"" << expectation.type << "\" " << expectation.points << " points, got \"" << type << "\" "
				<< reward.points << " (" << MatchKindName(reward.classification.kind) << ")\n";
		}
	}
}

int main(int argc, char** argv)
{
	TestStrings();
	TestClassification();
	TestMultiplier();
	TestConfigErrors();
	TestPendingPayouts();

	if (argc > 1)
		TestConfigFile(argv[1], argc > 2 && std::string(argv[2]) == "--shipped");

	std::cout << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
	return g_failures == 0 ? 0 : 1;
}
