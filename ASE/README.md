# PlayerStatPerks (ASE)

An [ArkServerApi (AseApi)](https://github.com/ArkServerApi/AseApi) plugin for ARK: Survival Evolved servers. It sets
each player's **base stats** and **per-level stat gains** from the [Permissions](https://ark-server-api.com/threads/ase-permissions.38/)
groups they are in. Players are identified by Steam ID. Groups can be permanent, tribe or timed groups; when a timed group starts or expires,
the player's stats follow within one recheck.

The ARK: Survival Ascended build lives in `..\ASA`. The perk logic, tests and configs are the same in both.

## How stats are applied

There are no gameplay hooks. ArkApi's one-second timer drives a scan of the online players every
`PollInterval_Seconds`. A player's stats are applied:

1. after a login or respawn, once `SpawnSettle_Seconds` have passed;
2. when their groups change, checked every `GroupRecheck_Seconds` (every poll, about 2 seconds, with the
   shipped config). The ASE Permissions plugin has no group-change notification, so this recheck catches
   both a group being added (including `permissions.addtimed` from an ArkShop kit; no reload needed) and a
   timed group expiring;
3. when the game has overwritten the values this plugin wrote.

For each managed stat the plugin writes the base value, the per-level gain and the resulting maximum.
Everything is computed from the character class defaults and the server's `PlayerBaseStatMultipliers` and
`PerLevelStatsMultiplier_Player`, never from the live values, so applying again cannot stack on itself
and a character never keeps a perk after the group that gave it expires. Pool stats (health, stamina,
food, water, oxygen) keep the same fill ratio when their maximum changes.

## Build

Requirements: Visual Studio 2026 with the "Desktop development with C++" workload (v145 toolset), and Git.

ArkServerApi is a git submodule at `extern\AseApi`. After cloning the repository:

```
git submodule update --init --recursive
```

Open `PlayerStatPerks.sln`, select **Release / x64**, build. `ArkApi.lib` ships in the AseApi repository at
`extern\AseApi\out_lib\`, so nothing has to be built first.

No vcpkg packages are needed: ArkServerApi bundles the fmt copy its headers use. `lib\Permissions.lib` is the
ASE Permissions plugin's import library and is committed in the repository.
`Permissions.dll` is delay-loaded, because ArkApi may load this plugin before Permissions.

The result is `out\PlayerStatPerks\`, containing `PlayerStatPerks.dll`, its `.pdb`, `PluginInfo.json`,
`config.json` and `config.commented.json`.

Build against the ArkServerApi version your servers run. To move to a different release, check out that tag
in the submodule and commit the new submodule pointer:

```
cd extern\AseApi
git fetch --tags
git checkout <tag>
cd ..\..
git add extern\AseApi
```

## Install

The server machine needs a Microsoft Visual C++ x64 Redistributable at least as new as the Visual Studio
the plugin was built with. A machine with that Visual Studio installed already has it; otherwise install
the latest one from Microsoft.

1. Remove any other plugin that sets player stats. Running two would have both writing the same values.
2. Copy the `out\PlayerStatPerks` folder to `ShooterGame\Binaries\Win64\ArkApi\Plugins\` on each server.
   The folder and the DLL must keep the same name.
3. Permissions must be installed and working on that server.
4. Start the server. `ArkApi.log` shows `Config loaded: ...` when the plugin is ready, then
   `Applied [Default, ...] to <Steam ID>` as players join.

To update a running server, drop the new build in as `PlayerStatPerks.dll.ArkApi` (the build produces that
copy too). If automatic plugin reloading is enabled in ArkApi's own `config.json`, ArkApi swaps it in and
online players are recalculated by the new version.

## config.json

Comments (`//` and `/* */`) are allowed in this file.

`configs\config.commented.json` holds the same settings and values as `config.json`, with an explanation
on every setting. It is a reference copy: the plugin only reads `config.json`.

### Timing

All optional, whole seconds.

| Setting | Default | Meaning |
|---|---|---|
| `PollInterval_Seconds` | `2` | How often online players are scanned. At least 1. |
| `SpawnSettle_Seconds` | `3` | Wait after a login or respawn before applying. |
| `GroupRecheck_Seconds` | `2` (shipped config) | How often each player's groups are read again. This is the delay before a group being added (e.g. an ArkShop kit) or a timed group expiring takes effect. The shipped ASE config sets it equal to `PollInterval_Seconds`, so groups are read on every poll; that is cheap because Permissions keeps groups in memory. If left out of the config it defaults to 10. At least 1. |

### Groups

| Setting | Default | Meaning |
|---|---|---|
| `StackGroups` | `true` | `true`: every group the player is in applies, lowest `Priority` first. `false`: only the highest-priority group the player is in applies. |
| `Groups` | | One entry per Permissions group, described below. |

Each `Groups` entry:

| Setting | Meaning |
|---|---|
| `Group` | The Permissions group name. Not case-sensitive. `Default` matches every player. |
| `Priority` | Higher numbers apply later and win for `BaseStats`. |
| `Multipliers` | Per-level gain multiplier for each stat. `1` = the server's normal gain. Matching groups multiply together. Cannot be negative. |
| `BaseStats` | The stat's value before any level points, in the units the game shows. `0` = leave this stat's base alone. When several groups set the same stat, the highest priority wins. |

Stats, for both `Multipliers` and `BaseStats`: `PlayerHealth`, `PlayerStamina`, `PlayerWeight`,
`PlayerFood`, `PlayerOxygen`, `PlayerWater`, `PlayerSpeed`, `PlayerCraftingSkill`, `PlayerFortitude`.
`PlayerSpeed` and `PlayerCraftingSkill` are percent (`100` = 100%).

A multiplier also applies to level points the character has already spent, so stats follow the player's
current groups without a respec.

### Commands and Messages

| Setting | Default | Meaning |
|---|---|---|
| `Commands.ShowPerks` | `/perks` | Chat command that shows a player their groups and effective values. |
| `Messages.Sender` | `Perks` | Sender name shown in chat. |
| `Messages.ShowPerks.SendInChat` | `true` | `true`: reply in chat. `false`: on-screen notification in the colour `R`/`G`/`B` (0 to 1). |
| `Messages.ShowPerks.Message` | | Placeholders: `{groups}`; `{PlayerHealth}` … `{PlayerFortitude}` for the combined multiplier; `{PlayerHealth_Base}` … `{PlayerFortitude_Base}` for the configured base (`default` when 0). |

## Commands

| Command | Where | What it does |
|---|---|---|
| `/perks` | Chat (name set by `Commands.ShowPerks`) | Shows the player their groups, multipliers and configured bases. |
| `PlayerStatPerks.Reload` | RCON, or the in-game console as an admin (`cheat PlayerStatPerks.Reload`) | Re-reads `config.json` and recalculates online players on the next poll. If the file has a mistake the old settings stay active and the error is reported. Only needed after editing the config, not after group changes, so ArkShop kits don't need to run it. |

## Troubleshooting

- **`Config not loaded: ...` at startup.** The message says what is wrong. Fix the file and run
  `PlayerStatPerks.Reload`. Until then player stats are left alone.
- **The plugin fails to load with error 126.** `Permissions.dll` could not be found when it was first
  needed. Check Permissions is installed in its own plugin folder and loads without errors.
- **Stats don't change after a timed group is added.** Wait for `GroupRecheck_Seconds` (2 seconds with
  the shipped config), then check `/perks` lists the group. If it doesn't, the player is not in that group as far as Permissions knows.
- **`Config: <group>: unknown multiplier/base stat '...' is ignored`.** A stat name is misspelled; the stat
  keeps its default for that group. The names are listed under Groups above.

## Tests

`tests\logic_tests.cpp` covers config parsing, warnings, group stacking, unit conversion and the
`/perks` text. It needs no game and no Windows. From a Visual Studio developer prompt in this folder:

```
cl /std:c++20 /EHsc /I extern\AseApi\version\Core\Public /I src tests\logic_tests.cpp
logic_tests.exe configs\config.json
```

Passing a config file validates it and prints any warnings, which is a quick way to check an edit before
putting it on a server.

Add `--shipped` after the path to also check the file against the values expected from the shipped
config. Run it on both shipped files after changing either, since they must hold the same values:

```
logic_tests.exe configs\config.json --shipped
logic_tests.exe configs\config.commented.json --shipped
```

## Files

| File | Purpose |
|---|---|
| `src\Main.cpp` | Player polling, stat application, `/perks`, reload commands. |
| `src\PerkLogic.h` | Config parsing, group resolution and stacking, unit conversion. No game dependencies. |
| `src\PermissionsApi.h` | The Permissions export this plugin uses (`GetPlayerGroups`). |
| `lib\Permissions.lib` | Import library for `Permissions.dll`. |
| `configs\config.json` | Default configuration. |
| `configs\config.commented.json` | The same configuration with every setting explained. Not read by the plugin. |
| `configs\PluginInfo.json` | Plugin metadata read by ArkApi. |
| `tests\logic_tests.cpp` | Stand-alone tests for `PerkLogic.h` and the shipped config. |
| `extern\AseApi` | ArkServerApi submodule the plugin builds against. |
