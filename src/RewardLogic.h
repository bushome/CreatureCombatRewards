#pragma once

// Pure reward logic for CreatureCombatRewards: config parsing, creature classification and point lookup.
//
// This header deliberately has no dependency on the game or on AsaApi game types (only on the
// nlohmann json.hpp that ships with AsaApi), so it can be compiled and unit-tested on any machine.
// See tests/logic_tests.cpp.

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "json.hpp"

namespace CombatRewards
{
	// Spelled out rather than std::numeric_limits<int>::max() because <Windows.h> defines a max() macro.
	constexpr int kIntMax = 2147483647;

	// ------------------------------------------------------------------------------------------
	// String helpers
	// ------------------------------------------------------------------------------------------

	inline std::string ToLower(std::string_view text)
	{
		std::string out(text);
		for (char& c : out)
		{
			if (c >= 'A' && c <= 'Z')
				c = static_cast<char>(c - 'A' + 'a');
		}
		return out;
	}

	inline std::string Trim(std::string_view text)
	{
		const char* whitespace = " \t\r\n";
		const size_t first = text.find_first_not_of(whitespace);
		if (first == std::string_view::npos)
			return {};
		const size_t last = text.find_last_not_of(whitespace);
		return std::string(text.substr(first, last - first + 1));
	}

	inline bool EndsWith(std::string_view text, std::string_view suffix)
	{
		return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
	}

	inline bool StartsWith(std::string_view text, std::string_view prefix)
	{
		return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
	}

	/**
	 * \brief Wildcard match. '*' matches any run of characters (including none), '?' matches exactly one.
	 * Both arguments are expected to be lower-cased already.
	 */
	inline bool WildcardMatch(std::string_view pattern, std::string_view text)
	{
		size_t p = 0, t = 0;
		size_t star = std::string_view::npos, star_text = 0;

		while (t < text.size())
		{
			if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t]))
			{
				++p;
				++t;
			}
			else if (p < pattern.size() && pattern[p] == '*')
			{
				star = p++;
				star_text = t;
			}
			else if (star != std::string_view::npos)
			{
				p = star + 1;
				t = ++star_text;
			}
			else
			{
				return false;
			}
		}

		while (p < pattern.size() && pattern[p] == '*')
			++p;

		return p == pattern.size();
	}

	/**
	 * \brief Replaces {key} placeholders. Unknown placeholders are left untouched.
	 */
	inline std::string FormatTemplate(std::string text,
		const std::vector<std::pair<std::string, std::string>>& values)
	{
		for (const auto& [key, value] : values)
		{
			const std::string token = "{" + key + "}";
			size_t pos = 0;
			while ((pos = text.find(token, pos)) != std::string::npos)
			{
				text.replace(pos, token.size(), value);
				pos += value.size();
			}
		}
		return text;
	}

	/**
	 * \brief Lower-cases a blueprint class name and drops the "_C" suffix:
	 * "Rex_Character_BP_C" -> "rex_character_bp".
	 */
	inline std::string NormalizeClassName(std::string_view name)
	{
		std::string out = ToLower(Trim(name));
		if (EndsWith(out, "_c"))
			out.resize(out.size() - 2);
		return out;
	}

	/**
	 * \brief Reduces every common way of writing a blueprint path to the same string:
	 * "Blueprint'/Game/PrimalEarth/Dinos/Rex/Rex_Character_BP.Rex_Character_BP'"
	 * "/Game/PrimalEarth/Dinos/Rex/Rex_Character_BP.Rex_Character_BP_C"
	 * "/Game/PrimalEarth/Dinos/Rex/Rex_Character_BP"
	 * all become "/game/primalearth/dinos/rex/rex_character_bp".
	 */
	inline std::string NormalizeBlueprintPath(std::string_view path)
	{
		std::string out = ToLower(Trim(path));

		const size_t open_quote = out.find('\'');
		if (open_quote != std::string::npos)
		{
			out.erase(0, open_quote + 1);
			const size_t close_quote = out.find('\'');
			if (close_quote != std::string::npos)
				out.erase(close_quote);
		}

		const size_t last_slash = out.find_last_of('/');
		const size_t dot = out.find('.', last_slash == std::string::npos ? 0 : last_slash);
		if (dot != std::string::npos)
			out.erase(dot);

		if (EndsWith(out, "_c"))
			out.resize(out.size() - 2);

		return out;
	}

	// ------------------------------------------------------------------------------------------
	// Settings
	// ------------------------------------------------------------------------------------------

	struct CreaturePattern
	{
		std::string raw;       // exactly as written in config.json (used for logging)
		std::string lowered;   // normalised form used for matching
		bool is_path = false;  // contains '/', so it is matched against the blueprint path
		bool has_wildcard = false;
	};

	inline CreaturePattern MakePattern(const std::string& entry)
	{
		CreaturePattern pattern;
		pattern.raw = Trim(entry);
		pattern.is_path = pattern.raw.find('/') != std::string::npos;
		pattern.has_wildcard = pattern.raw.find_first_of("*?") != std::string::npos;

		if (pattern.has_wildcard)
		{
			// Keep the wildcard text as typed, apart from case and the Blueprint'...' wrapper.
			pattern.lowered = ToLower(pattern.raw);
			if (pattern.is_path)
			{
				const size_t open_quote = pattern.lowered.find('\'');
				if (open_quote != std::string::npos)
				{
					pattern.lowered.erase(0, open_quote + 1);
					const size_t close_quote = pattern.lowered.find('\'');
					if (close_quote != std::string::npos)
						pattern.lowered.erase(close_quote);
				}
			}
		}
		else
		{
			pattern.lowered = pattern.is_path ? NormalizeBlueprintPath(pattern.raw) : NormalizeClassName(pattern.raw);
		}

		return pattern;
	}

	struct LevelRange
	{
		int min_level = 1;
		int max_level = kIntMax;
		int points = 0;
	};

	struct CreatureType
	{
		std::string name;
		std::vector<CreaturePattern> creatures;
		std::vector<LevelRange> level_ranges;
	};

	struct NotificationSettings
	{
		bool enabled = true;
		bool use_chat = false;
		std::string sender = "Combat Rewards";
		std::string message = "+{points} points - {creature} (Lvl {level})";
		float display_time = 4.0f;
		float text_scale = 1.2f;
	};

	struct Settings
	{
		int payout_interval_seconds = 30;
		double points_multiplier = 1.0;
		bool reward_unridden_tame_kills = false;
		double tame_kill_max_distance = 15000.0;
		bool log_kills = false;

		NotificationSettings notifications;

		std::vector<CreatureType> creature_types;
		std::vector<CreaturePattern> excluded;

		// Index into creature_types, or -1 for "no reward".
		int fallback_carnivore_type = -1;
		int fallback_herbivore_type = -1;

		// Non-fatal problems found while reading the config. The plugin logs these.
		std::vector<std::string> warnings;
	};

	namespace detail
	{
		inline const char* JsonTypeName(const nlohmann::json& value)
		{
			return value.type_name();
		}

		template <typename T>
		T ReadValue(const nlohmann::json& object, const char* key, T fallback, const std::string& where)
		{
			const auto it = object.find(key);
			if (it == object.end() || it->is_null())
				return fallback;

			try
			{
				return it->get<T>();
			}
			catch (const nlohmann::json::exception&)
			{
				throw std::runtime_error(where + "\"" + key + "\" has the wrong type (found " + JsonTypeName(*it) + ")");
			}
		}

		inline int ReadInt(const nlohmann::json& object, const char* key, int fallback, const std::string& where)
		{
			const auto it = object.find(key);
			if (it == object.end() || it->is_null())
				return fallback;
			if (!it->is_number_integer())
				throw std::runtime_error(where + "\"" + key + "\" must be a whole number (found " + JsonTypeName(*it) + ")");
			return it->get<int>();
		}

		inline const nlohmann::json& ReadSection(const nlohmann::json& object, const char* key, bool array,
			const nlohmann::json& empty, const std::string& where)
		{
			const auto it = object.find(key);
			if (it == object.end() || it->is_null())
				return empty;
			if (array ? !it->is_array() : !it->is_object())
				throw std::runtime_error(where + "\"" + key + "\" must be " + (array ? "a list [...]" : "an object {...}"));
			return *it;
		}

		inline std::vector<CreaturePattern> ReadPatterns(const nlohmann::json& list, const std::string& where)
		{
			std::vector<CreaturePattern> patterns;
			for (const auto& entry : list)
			{
				if (!entry.is_string())
					throw std::runtime_error(where + "every entry must be a text string (found " + JsonTypeName(entry) + ")");

				CreaturePattern pattern = MakePattern(entry.get<std::string>());
				if (!pattern.lowered.empty())
					patterns.push_back(std::move(pattern));
			}
			return patterns;
		}
	} // namespace detail

	/**
	 * \brief Builds Settings from parsed config.json. Throws std::runtime_error with a readable
	 * message if the config cannot be used.
	 */
	inline Settings ParseSettings(const nlohmann::json& root)
	{
		using detail::ReadInt;
		using detail::ReadSection;
		using detail::ReadValue;

		if (!root.is_object())
			throw std::runtime_error("config.json must contain a JSON object at the top level");

		const nlohmann::json empty_object = nlohmann::json::object();
		const nlohmann::json empty_array = nlohmann::json::array();

		Settings settings;

		// --- General ---
		const auto& general = ReadSection(root, "General", false, empty_object, "");
		settings.payout_interval_seconds = ReadInt(general, "PayoutIntervalSeconds", settings.payout_interval_seconds, "General: ");
		settings.points_multiplier = ReadValue<double>(general, "PointsMultiplier", settings.points_multiplier, "General: ");
		settings.reward_unridden_tame_kills = ReadValue<bool>(general, "RewardUnriddenTameKills", settings.reward_unridden_tame_kills, "General: ");
		settings.tame_kill_max_distance = ReadValue<double>(general, "TameKillMaxDistance", settings.tame_kill_max_distance, "General: ");
		settings.log_kills = ReadValue<bool>(general, "LogKills", settings.log_kills, "General: ");

		if (settings.payout_interval_seconds < 0)
			throw std::runtime_error("General: \"PayoutIntervalSeconds\" cannot be negative");
		if (settings.points_multiplier < 0.0)
			throw std::runtime_error("General: \"PointsMultiplier\" cannot be negative");
		if (settings.tame_kill_max_distance < 0.0)
			throw std::runtime_error("General: \"TameKillMaxDistance\" cannot be negative");

		// --- Notifications ---
		const auto& notify = ReadSection(root, "Notifications", false, empty_object, "");
		auto& n = settings.notifications;
		n.enabled = ReadValue<bool>(notify, "Enabled", n.enabled, "Notifications: ");
		n.use_chat = ReadValue<bool>(notify, "UseChat", n.use_chat, "Notifications: ");
		n.sender = ReadValue<std::string>(notify, "Sender", n.sender, "Notifications: ");
		n.message = ReadValue<std::string>(notify, "Message", n.message, "Notifications: ");
		n.display_time = ReadValue<float>(notify, "DisplayTime", n.display_time, "Notifications: ");
		n.text_scale = ReadValue<float>(notify, "TextScale", n.text_scale, "Notifications: ");

		// --- CreatureTypes ---
		const auto& types = ReadSection(root, "CreatureTypes", true, empty_array, "");
		for (size_t i = 0; i < types.size(); ++i)
		{
			const auto& entry = types[i];
			std::string where = "CreatureTypes[" + std::to_string(i) + "]: ";
			if (!entry.is_object())
				throw std::runtime_error(where + "must be an object {...}");

			CreatureType type;
			type.name = Trim(ReadValue<std::string>(entry, "Name", "", where));
			if (type.name.empty())
				throw std::runtime_error(where + "\"Name\" is missing or empty");

			where = "CreatureTypes \"" + type.name + "\": ";

			for (const auto& existing : settings.creature_types)
			{
				if (ToLower(existing.name) == ToLower(type.name))
					throw std::runtime_error(where + "this name is used more than once");
			}

			type.creatures = detail::ReadPatterns(ReadSection(entry, "Creatures", true, empty_array, where), where + "Creatures: ");

			const auto& ranges = ReadSection(entry, "LevelRanges", true, empty_array, where);
			for (size_t r = 0; r < ranges.size(); ++r)
			{
				const std::string range_where = where + "LevelRanges[" + std::to_string(r) + "]: ";
				if (!ranges[r].is_object())
					throw std::runtime_error(range_where + "must be an object {...}");

				LevelRange range;
				range.min_level = ReadInt(ranges[r], "MinLevel", 1, range_where);
				// MaxLevel left out (or 0) means "this level and above".
				const int max_level = ReadInt(ranges[r], "MaxLevel", 0, range_where);
				range.max_level = max_level <= 0 ? kIntMax : max_level;

				if (ranges[r].find("Points") == ranges[r].end())
					throw std::runtime_error(range_where + "\"Points\" is missing");
				range.points = ReadInt(ranges[r], "Points", 0, range_where);

				if (range.points < 0)
					throw std::runtime_error(range_where + "\"Points\" cannot be negative");
				if (range.min_level > range.max_level)
					throw std::runtime_error(range_where + "\"MinLevel\" is higher than \"MaxLevel\"");

				for (const auto& earlier : type.level_ranges)
				{
					if (range.min_level <= earlier.max_level && earlier.min_level <= range.max_level)
					{
						settings.warnings.push_back(range_where + "overlaps an earlier range; the earlier range wins for the shared levels");
						break;
					}
				}

				type.level_ranges.push_back(range);
			}

			if (type.level_ranges.empty())
				settings.warnings.push_back(where + "has no LevelRanges, so it never pays out");

			settings.creature_types.push_back(std::move(type));
		}

		if (settings.creature_types.empty())
			settings.warnings.push_back("No CreatureTypes are defined, so no kill pays out");

		// --- Excluded ---
		settings.excluded = detail::ReadPatterns(ReadSection(root, "Excluded", true, empty_array, ""), "Excluded: ");

		// --- Fallback ---
		const auto& fallback = ReadSection(root, "Fallback", false, empty_object, "");
		const auto resolve_type = [&settings](const std::string& name, const char* key) -> int
		{
			const std::string wanted = ToLower(Trim(name));
			if (wanted.empty())
				return -1;

			for (size_t i = 0; i < settings.creature_types.size(); ++i)
			{
				if (ToLower(settings.creature_types[i].name) == wanted)
					return static_cast<int>(i);
			}

			throw std::runtime_error(std::string("Fallback: \"") + key + "\" refers to \"" + name +
				"\", which is not the Name of any entry in CreatureTypes");
		};

		settings.fallback_carnivore_type = resolve_type(ReadValue<std::string>(fallback, "Carnivores", "", "Fallback: "), "Carnivores");
		settings.fallback_herbivore_type = resolve_type(ReadValue<std::string>(fallback, "Herbivores", "", "Fallback: "), "Herbivores");

		return settings;
	}

	// ------------------------------------------------------------------------------------------
	// Classification
	// ------------------------------------------------------------------------------------------

	struct CreatureInfo
	{
		// Blueprint class names, most specific first, e.g.
		// { "MegaRex_Character_BP_C", "Rex_Character_BP_C", "Dino_Character_BP_C" }.
		std::vector<std::string> class_chain;
		std::string name_tag;   // e.g. "Rex"
		std::string blueprint;  // e.g. "Blueprint'/Game/PrimalEarth/Dinos/Rex/Rex_Character_BP.Rex_Character_BP'"
		bool is_carnivore = false;
		int level = 0;
	};

	enum class MatchKind
	{
		None,      // not listed and no fallback applies -> no reward
		Excluded,  // matched the Excluded list -> no reward
		Listed,    // matched an entry of a creature type
		Fallback   // not listed anywhere; the Fallback section picked the type
	};

	struct Classification
	{
		MatchKind kind = MatchKind::None;
		int type_index = -1;
		std::string matched_entry; // the config entry that matched, as written
	};

	namespace detail
	{
		struct Identity
		{
			std::string class_name; // normalised (no _C)
			std::string name_tag;   // normalised, only set for the creature's own class
			std::string path;       // normalised, only set for the creature's own class
		};

		inline bool Matches(const CreaturePattern& pattern, const Identity& id)
		{
			if (pattern.is_path)
			{
				if (id.path.empty())
					return false;
				return pattern.has_wildcard ? WildcardMatch(pattern.lowered, id.path) : pattern.lowered == id.path;
			}

			if (pattern.has_wildcard)
			{
				return WildcardMatch(pattern.lowered, id.class_name)
					|| WildcardMatch(pattern.lowered, id.class_name + "_c")
					|| (!id.name_tag.empty() && WildcardMatch(pattern.lowered, id.name_tag));
			}

			return pattern.lowered == id.class_name || (!id.name_tag.empty() && pattern.lowered == id.name_tag);
		}

		inline const CreaturePattern* FindMatch(const std::vector<CreaturePattern>& patterns, const Identity& id)
		{
			for (const auto& pattern : patterns)
			{
				if (Matches(pattern, id))
					return &pattern;
			}
			return nullptr;
		}
	} // namespace detail

	/**
	 * \brief Decides which creature type (if any) a creature belongs to.
	 *
	 * The creature's own class is checked first, then each parent class in turn, so the most specific
	 * entry wins: "MegaRex_Character_BP" listed under one type beats "Rex_Character_BP" listed under
	 * another, whatever order the types are in. At the same level, Excluded beats the creature types,
	 * and creature types are checked in the order they appear in the config.
	 */
	inline Classification Classify(const Settings& settings, const CreatureInfo& creature)
	{
		Classification result;

		const size_t levels = creature.class_chain.empty() ? 1 : creature.class_chain.size();
		for (size_t level = 0; level < levels; ++level)
		{
			detail::Identity id;
			if (level < creature.class_chain.size())
				id.class_name = NormalizeClassName(creature.class_chain[level]);

			if (level == 0)
			{
				id.name_tag = ToLower(Trim(creature.name_tag));
				id.path = NormalizeBlueprintPath(creature.blueprint);
			}

			if (const auto* match = detail::FindMatch(settings.excluded, id))
			{
				result.kind = MatchKind::Excluded;
				result.matched_entry = match->raw;
				return result;
			}

			for (size_t i = 0; i < settings.creature_types.size(); ++i)
			{
				if (const auto* match = detail::FindMatch(settings.creature_types[i].creatures, id))
				{
					result.kind = MatchKind::Listed;
					result.type_index = static_cast<int>(i);
					result.matched_entry = match->raw;
					return result;
				}
			}
		}

		const int fallback = creature.is_carnivore ? settings.fallback_carnivore_type : settings.fallback_herbivore_type;
		if (fallback >= 0 && fallback < static_cast<int>(settings.creature_types.size()))
		{
			result.kind = MatchKind::Fallback;
			result.type_index = fallback;
		}

		return result;
	}

	/**
	 * \brief Points a creature type pays for a given level, before the multiplier. 0 if no range covers it.
	 */
	inline int BasePointsForLevel(const CreatureType& type, int level)
	{
		for (const auto& range : type.level_ranges)
		{
			if (level >= range.min_level && level <= range.max_level)
				return range.points;
		}
		return 0;
	}

	inline int ApplyMultiplier(int base_points, double multiplier)
	{
		if (base_points <= 0 || multiplier <= 0.0)
			return 0;

		const double scaled = std::round(static_cast<double>(base_points) * multiplier);
		if (scaled >= static_cast<double>(kIntMax))
			return kIntMax;

		return static_cast<int>(scaled);
	}

	struct Reward
	{
		Classification classification;
		int points = 0;
	};

	/**
	 * \brief Full decision for one kill: which type the creature falls under and what it pays.
	 */
	inline Reward Evaluate(const Settings& settings, const CreatureInfo& creature)
	{
		Reward reward;
		reward.classification = Classify(settings, creature);

		if (reward.classification.type_index >= 0)
		{
			const auto& type = settings.creature_types[static_cast<size_t>(reward.classification.type_index)];
			reward.points = ApplyMultiplier(BasePointsForLevel(type, creature.level), settings.points_multiplier);
		}

		return reward;
	}

	inline const char* MatchKindName(MatchKind kind)
	{
		switch (kind)
		{
		case MatchKind::Excluded: return "excluded";
		case MatchKind::Listed: return "listed";
		case MatchKind::Fallback: return "fallback";
		default: return "unlisted";
		}
	}

	// ------------------------------------------------------------------------------------------
	// Pending payouts
	// ------------------------------------------------------------------------------------------

	/**
	 * \brief Points earned but not yet handed to ArkShop, per player. Batching keeps one busy fight
	 * from turning into dozens of database writes and chat lines.
	 */
	class PendingPayouts
	{
	public:
		struct Entry
		{
			long long points = 0;
			int failed_attempts = 0;
		};

		void Add(const std::string& eos_id, int points)
		{
			if (eos_id.empty() || points <= 0)
				return;
			entries_[eos_id].points += points;
		}

		bool Empty() const
		{
			return entries_.empty();
		}

		long long TotalPoints() const
		{
			long long total = 0;
			for (const auto& [eos_id, entry] : entries_)
				total += entry.points;
			return total;
		}

		/**
		 * \brief Tries to pay every pending entry. `pay(eos_id, points)` returns true on success.
		 * Entries that fail stay queued and are dropped after `max_attempts` failures;
		 * `dropped(eos_id, points)` is called for each one that is given up on.
		 */
		template <typename PayFn, typename DroppedFn>
		void Flush(PayFn&& pay, DroppedFn&& dropped, int max_attempts = 3)
		{
			for (auto it = entries_.begin(); it != entries_.end();)
			{
				const long long clamped = it->second.points > kIntMax ? kIntMax : it->second.points;
				if (pay(it->first, static_cast<int>(clamped)))
				{
					it = entries_.erase(it);
					continue;
				}

				if (++it->second.failed_attempts >= max_attempts)
				{
					dropped(it->first, static_cast<int>(clamped));
					it = entries_.erase(it);
					continue;
				}

				++it;
			}
		}

	private:
		std::unordered_map<std::string, Entry> entries_;
	};
} // namespace CombatRewards
