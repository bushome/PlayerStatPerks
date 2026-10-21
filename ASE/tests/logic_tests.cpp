// Stand-alone tests for src/PerkLogic.h. No game, ArkServerApi DLL or Windows needed.
//
// Build and run from the project folder (any C++20 compiler):
//   cl /std:c++20 /EHsc /I extern\AseApi\version\Core\Public /I src tests\logic_tests.cpp && logic_tests.exe configs\config.json
//   clang++ -std=c++20 -I extern/AseApi/version/Core/Public -I src tests/logic_tests.cpp -o logic_tests && ./logic_tests configs/config.json
//
// The optional argument is a config.json to validate. Add --shipped after it to also check the file
// against the expectations for the shipped config at the bottom of this file:
//   logic_tests.exe configs\config.json --shipped
// configs\config.commented.json holds the same values, so it must pass the same check.

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

#include "PerkLogic.h"

using namespace StatPerks;

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

static bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }

static Settings Parse(const char* text)
{
	return ParseSettings(nlohmann::json::parse(text, nullptr, true, true));
}

static void TestParsing()
{
	const Settings s = Parse(R"({
		"Groups": [
			{ "Group": "High", "Priority": 5, "Multipliers": { "PlayerWeight": 2 } },
			{ "Group": "Low",  "Priority": 1, "BaseStats": { "PlayerHealth": 250 } }
		]
	})");
	CHECK(s.groups.size() == 2);
	CHECK(s.groups[0].group == "Low");            // sorted by priority
	CHECK(Near(s.groups[1].multipliers[Weight], 2.0));
	CHECK(Near(s.groups[1].multipliers[Health], 1.0)); // missing keys default to 1
	CHECK(Near(s.groups[0].base_stats[Health], 250));
	CHECK(Near(s.groups[1].base_stats[Health], 0));    // missing keys default to 0
	CHECK(s.stack_groups);
	CHECK(s.perks_command == "/perks");
	CHECK(s.poll_interval_seconds == 2 && s.spawn_settle_seconds == 3 && s.group_recheck_seconds == 10);
	CHECK(s.warnings.empty());
}

static void TestWarnings()
{
	const Settings s = Parse(R"({
		"Timing": { "PollInterval_Seconds": 0 },
		"Groups": [
			{ "Group": "A", "Multipliers": { "PlayerWieght": 2, "PlayerFood": -1 }, "BaseStats": { "Health": 200 } },
			{ "Group": "a" },
			{ "Priority": 3 }
		]
	})");
	// multiplier typo, base stat typo, negative, duplicate, no name
	CHECK(s.warnings.size() == 5);
	CHECK(s.groups.size() == 2);
	CHECK(Near(s.groups[0].multipliers[Food], 0.0));   // negative clamped
	CHECK(s.poll_interval_seconds == 1);               // clamped to at least 1
}

static void TestResolve()
{
	const Settings s = Parse(R"({
		"StackGroups": true,
		"Groups": [
			{ "Group": "Default", "Priority": 1, "Multipliers": { "PlayerWeight": 1 },   "BaseStats": { "PlayerWeight": 150 } },
			{ "Group": "Vip",     "Priority": 2, "Multipliers": { "PlayerWeight": 1.5 }, "BaseStats": { "PlayerWeight": 200 } },
			{ "Group": "Mega",    "Priority": 3, "Multipliers": { "PlayerWeight": 2 } }
		]
	})");

	const Effective none = Resolve(s, {});
	CHECK(none.matched_groups.size() == 1);
	CHECK(Near(*none.base_stats[Weight], 150));
	CHECK(!none.base_stats[Health]);

	const Effective both = Resolve(s, { "VIP", "mega" });  // case-insensitive
	CHECK(both.matched_groups.size() == 3);
	CHECK(Near(both.multipliers[Weight], 3.0));          // 1 * 1.5 * 2
	CHECK(Near(*both.base_stats[Weight], 200));          // Mega sets 0, Vip's 200 stands

	Settings flat = s;
	flat.stack_groups = false;
	const Effective top = Resolve(flat, { "Vip", "Mega" });
	CHECK(top.matched_groups.size() == 1 && top.matched_groups[0] == "Mega");
	CHECK(Near(top.multipliers[Weight], 2.0));
	CHECK(!top.base_stats[Weight]);

	CHECK(Resolve(s, { "Vip" }).Signature() != none.Signature());  // timed group expiry is detected
	CHECK(Resolve(s, { "Unknown" }).Signature() == none.Signature());
}

static void TestConversions()
{
	CHECK(Near(ToEngineBase(Speed, 100, 1.0), 1.0));
	CHECK(Near(ToEngineBase(Speed, 100, 0.0), 0.0));
	CHECK(Near(ToEngineBase(CraftingSkill, 2015, 1.0), 20.15));
	CHECK(Near(ToEngineBase(Health, 100, 100), 100));
	CHECK(FormatNumber(1.2) == "1.2");
	CHECK(FormatNumber(1.0) == "1");
	CHECK(FormatNumber(0.25) == "0.25");

	Effective e;
	e.multipliers.fill(1.0);
	e.multipliers[Weight] = 1.2;
	e.base_stats[CraftingSkill] = 2015;
	CHECK(FormatPerks("{groups} {PlayerWeight} {PlayerWeight_Base} {PlayerCraftingSkill_Base}", e)
		== "Default 1.2 default 2015%");
}

// Expectations for configs/config.json and configs/config.commented.json as shipped.
static void CheckShipped(const Settings& s)
{
	CHECK(s.groups.size() == 3);
	CHECK(s.stack_groups);

	const Effective def = Resolve(s, {});
	CHECK(Near(*def.base_stats[Weight], 150));
	CHECK(Near(*def.base_stats[Speed], 100));
	CHECK(!def.base_stats[CraftingSkill]);

	const Effective patron = Resolve(s, { "Patrons" });
	CHECK(Near(patron.multipliers[Weight], 1.2));
	CHECK(Near(*patron.base_stats[Weight], 150));

	const Effective crafter = Resolve(s, { "Patrons", "CraftingSkill" });
	CHECK(Near(crafter.multipliers[Weight], 1.2 * 0.2));
	CHECK(Near(crafter.multipliers[Fortitude], 0.0));
	CHECK(Near(*crafter.base_stats[CraftingSkill], 2015));
}

int main(int argc, char** argv)
{
	TestParsing();
	TestWarnings();
	TestResolve();
	TestConversions();

	if (argc >= 2)
	{
		std::ifstream file(argv[1]);
		if (!file.is_open())
		{
			std::cerr << "cannot open " << argv[1] << "\n";
			return 2;
		}
		try
		{
			const Settings s = ParseSettings(nlohmann::json::parse(file, nullptr, true, true));
			std::cout << argv[1] << ": " << s.groups.size() << " group(s)\n";
			for (const auto& warning : s.warnings)
				std::cout << "  warning: " << warning << "\n";
			if (argc >= 3 && std::string(argv[2]) == "--shipped")
				CheckShipped(s);
		}
		catch (const std::exception& error)
		{
			std::cerr << argv[1] << ": " << error.what() << "\n";
			return 2;
		}
	}

	std::cout << g_checks - g_failures << "/" << g_checks << " checks passed\n";
	return g_failures == 0 ? 0 : 1;
}
