#pragma once

// Pure perk logic for PlayerStatPerks: config parsing and permission-group resolution.
//
// This header deliberately has no dependency on the game or on server API game types (only on the
// nlohmann json.hpp that ships with AsaApi and AseApi), so it can be compiled and unit-tested on any
// machine, and shared unchanged by the ASA and ASE builds. See tests/logic_tests.cpp.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "json.hpp"

namespace StatPerks
{
	// ------------------------------------------------------------------------------------------
	// Stats
	// ------------------------------------------------------------------------------------------

	// The player stats this plugin manages. Order is internal only.
	enum Stat : int
	{
		Health = 0,
		Stamina,
		Weight,
		Food,
		Oxygen,
		Water,
		Speed,
		CraftingSkill,
		Fortitude,
		StatCount
	};

	struct StatInfo
	{
		const char* key;       // config key, e.g. "PlayerHealth"
		const char* label;     // in-game name
		int game_index;        // EPrimalCharacterStatusValue index (same on ASE and ASA)
		bool is_percent;       // configured in display percent (100 = 100%)
		bool scales_current;   // current value is a pool (health, food...) kept at the same fill ratio
	};

	inline const std::array<StatInfo, StatCount>& Stats()
	{
		static const std::array<StatInfo, StatCount> stats{ {
			{ "PlayerHealth",        "Health",         0,  false, true  },
			{ "PlayerStamina",       "Stamina",        1,  false, true  },
			{ "PlayerWeight",        "Weight",         7,  false, false },
			{ "PlayerFood",          "Food",           4,  false, true  },
			{ "PlayerOxygen",        "Oxygen",         3,  false, true  },
			{ "PlayerWater",         "Water",          5,  false, true  },
			{ "PlayerSpeed",         "Movement Speed", 9,  true,  false },
			{ "PlayerCraftingSkill", "Crafting Skill", 11, true,  false },
			{ "PlayerFortitude",     "Fortitude",      10, false, false },
		} };
		return stats;
	}

	// ------------------------------------------------------------------------------------------
	// Settings
	// ------------------------------------------------------------------------------------------

	struct GroupRule
	{
		std::string group;
		int priority = 0;
		std::array<double, StatCount> multipliers{};   // per-level gain multiplier, 1 = vanilla
		std::array<double, StatCount> base_stats{};    // 0 = leave the game's base alone
	};

	struct Message
	{
		bool send_in_chat = true;
		std::string text;
		float r = 0.f, g = 1.f, b = 0.f;
	};

	struct Settings
	{
		bool stack_groups = true;
		std::vector<GroupRule> groups;     // sorted by ascending priority

		int poll_interval_seconds = 2;     // how often online players are scanned
		int spawn_settle_seconds = 3;      // wait after a (re)spawn before applying
		int group_recheck_seconds = 10;    // how often group membership is re-read (timed groups)

		std::string perks_command = "/perks";
		std::string sender = "Perks";
		Message perks_message;

		std::vector<std::string> warnings;
	};

	inline std::string ToLower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	namespace detail
	{
		inline double NumOr(const nlohmann::json& obj, const char* key, double fallback)
		{
			auto it = obj.find(key);
			return (it != obj.end() && it->is_number()) ? it->get<double>() : fallback;
		}

		inline bool IsStatKey(const std::string& key)
		{
			for (const auto& info : Stats())
				if (key == info.key) return true;
			return false;
		}

		inline Message ReadMessage(const nlohmann::json& obj, const Message& fallback)
		{
			Message m = fallback;
			if (!obj.is_object()) return m;
			m.send_in_chat = obj.value("SendInChat", m.send_in_chat);
			m.text = obj.value("Message", m.text);
			m.r = static_cast<float>(NumOr(obj, "R", m.r));
			m.g = static_cast<float>(NumOr(obj, "G", m.g));
			m.b = static_cast<float>(NumOr(obj, "B", m.b));
			return m;
		}
	}

	// Parses config.json. Mistakes that would otherwise fail silently (a misspelled stat name, a
	// duplicate group, a negative multiplier) are reported in Settings::warnings.
	inline Settings ParseSettings(const nlohmann::json& root)
	{
		Settings cfg;

		cfg.stack_groups = root.value("StackGroups", true);

		for (const auto& g : root.value("Groups", nlohmann::json::array()))
		{
			GroupRule rule;
			rule.group = g.value("Group", "");
			rule.priority = g.value("Priority", 0);
			if (rule.group.empty())
			{
				cfg.warnings.push_back("a Groups entry has no Group name and is skipped");
				continue;
			}
			for (const auto& existing : cfg.groups)
				if (ToLower(existing.group) == ToLower(rule.group))
					cfg.warnings.push_back("group '" + rule.group + "' is listed more than once");

			const auto& mult = g.value("Multipliers", nlohmann::json::object());
			const auto& base = g.value("BaseStats", nlohmann::json::object());
			for (int i = 0; i < StatCount; ++i)
			{
				rule.multipliers[i] = detail::NumOr(mult, Stats()[i].key, 1.0);
				rule.base_stats[i] = detail::NumOr(base, Stats()[i].key, 0.0);
				if (rule.multipliers[i] < 0.0)
				{
					cfg.warnings.push_back(rule.group + ": Multipliers." + Stats()[i].key + " is negative, using 0");
					rule.multipliers[i] = 0.0;
				}
			}

			for (const auto& item : mult.items())
				if (!detail::IsStatKey(item.key()))
					cfg.warnings.push_back(rule.group + ": unknown multiplier '" + item.key() + "' is ignored");
			for (const auto& item : base.items())
				if (!detail::IsStatKey(item.key()))
					cfg.warnings.push_back(rule.group + ": unknown base stat '" + item.key() + "' is ignored");

			cfg.groups.push_back(std::move(rule));
		}
		std::stable_sort(cfg.groups.begin(), cfg.groups.end(),
			[](const GroupRule& a, const GroupRule& b) { return a.priority < b.priority; });

		const auto& timing = root.value("Timing", nlohmann::json::object());
		cfg.poll_interval_seconds = (std::max)(1, timing.value("PollInterval_Seconds", cfg.poll_interval_seconds));
		cfg.spawn_settle_seconds = (std::max)(0, timing.value("SpawnSettle_Seconds", cfg.spawn_settle_seconds));
		cfg.group_recheck_seconds = (std::max)(1, timing.value("GroupRecheck_Seconds", cfg.group_recheck_seconds));

		const auto& cmds = root.value("Commands", nlohmann::json::object());
		cfg.perks_command = cmds.value("ShowPerks", cfg.perks_command);

		const auto& msgs = root.value("Messages", nlohmann::json::object());
		cfg.sender = msgs.value("Sender", cfg.sender);
		cfg.perks_message = detail::ReadMessage(msgs.value("ShowPerks", nlohmann::json::object()), cfg.perks_message);

		if (cfg.groups.empty())
			cfg.warnings.push_back("no Groups are defined; player stats are left alone");

		return cfg;
	}

	// ------------------------------------------------------------------------------------------
	// Resolution
	// ------------------------------------------------------------------------------------------

	// Effective values for one player after all matching groups are combined.
	struct Effective
	{
		std::array<double, StatCount> multipliers{};
		std::array<std::optional<double>, StatCount> base_stats{};
		std::vector<std::string> matched_groups;    // in priority order

		// Stable key used to detect "nothing changed since last apply".
		std::string Signature() const
		{
			std::string s;
			for (const auto& g : matched_groups) { s += g; s += '|'; }
			return s;
		}
	};

	// player_groups: whatever the Permissions plugin reports (timed groups that are delayed or
	// expired are already filtered out by it). "Default" always counts.
	inline Effective Resolve(const Settings& cfg, const std::vector<std::string>& player_groups)
	{
		std::vector<std::string> owned;
		owned.reserve(player_groups.size() + 1);
		owned.push_back("default");
		for (const auto& g : player_groups) owned.push_back(ToLower(g));

		std::vector<const GroupRule*> matched;
		for (const auto& rule : cfg.groups)
			if (std::find(owned.begin(), owned.end(), ToLower(rule.group)) != owned.end())
				matched.push_back(&rule);

		// Without stacking only the highest-priority match counts.
		if (!cfg.stack_groups && matched.size() > 1)
			matched.erase(matched.begin(), matched.end() - 1);

		Effective eff;
		eff.multipliers.fill(1.0);
		for (const GroupRule* rule : matched)   // ascending priority: later = higher
		{
			eff.matched_groups.push_back(rule->group);
			for (int i = 0; i < StatCount; ++i)
			{
				eff.multipliers[i] *= rule->multipliers[i];
				if (rule->base_stats[i] != 0.0)   // higher priority overrides lower
					eff.base_stats[i] = rule->base_stats[i];
			}
		}
		return eff;
	}

	// Display units -> stored engine value for a configured base stat.
	// Percent stats (speed, crafting) are stored by the engine either as an absolute fraction
	// (1.0 = 100%) or as an offset from 100% (0.0 = 100%). Which one is detected from the class
	// default: a default below 0.5 can only be the offset form, since no player starts under 50%.
	inline double ToEngineBase(Stat stat, double configured, double class_default)
	{
		if (!Stats()[stat].is_percent) return configured;
		const double fraction = configured / 100.0;
		return class_default < 0.5 ? fraction - 1.0 : fraction;
	}

	// ------------------------------------------------------------------------------------------
	// /perks text
	// ------------------------------------------------------------------------------------------

	inline std::string FormatNumber(double v)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.2f", v);
		std::string s(buf);
		while (!s.empty() && s.back() == '0') s.pop_back();
		if (!s.empty() && s.back() == '.') s.pop_back();
		return s;
	}

	// Fills {groups}, {PlayerHealth} (multiplier) and {PlayerHealth_Base} (configured base, or
	// "default") placeholders.
	inline std::string FormatPerks(const std::string& tmpl, const Effective& eff)
	{
		auto replace_all = [](std::string& s, const std::string& from, const std::string& to)
		{
			for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size())
				s.replace(pos, from.size(), to);
		};

		std::string out = tmpl;
		std::string groups;
		for (const auto& g : eff.matched_groups) { if (!groups.empty()) groups += ", "; groups += g; }
		replace_all(out, "{groups}", groups.empty() ? "Default" : groups);

		for (int i = 0; i < StatCount; ++i)
		{
			const auto& info = Stats()[i];
			replace_all(out, std::string("{") + info.key + "_Base}",
				eff.base_stats[i] ? FormatNumber(*eff.base_stats[i]) + (info.is_percent ? "%" : "") : "default");
			replace_all(out, std::string("{") + info.key + "}", FormatNumber(eff.multipliers[i]));
		}
		return out;
	}
}
