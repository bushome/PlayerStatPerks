# PlayerStatPerks (ASA)

An [AsaApi](https://github.com/ArkServerApi/AsaApi) plugin for ARK: Survival Ascended servers. It sets each
player's **base stats** and **per-level stat gains** from the [Permissions](https://ark-server-api.com/resources/asa-permissions.33)
groups they are in. Groups can be permanent, tribe or timed groups; when a timed group starts or expires,
the player's stats follow within one recheck.

## How stats are applied

There are no gameplay hooks. AsaApi's one-second timer drives a scan of the online players every
`PollInterval_Seconds`. A player's stats are applied:

1. after a login or respawn, once `SpawnSettle_Seconds` have passed;
2. when their groups change. A group being added or removed (including `permissions.addtimed` from an
   ArkShop kit) is reported by Permissions and picked up on the next poll, so no reload is needed. A timed
   group expiring is caught by the recheck every `GroupRecheck_Seconds`;
3. when the game has overwritten the values this plugin wrote.

For each managed stat the plugin writes the base value, the per-level gain and the resulting maximum.
The maximum is calculated the way the game does it: (base + points x per-level gain x
`PerLevelStatsMultiplier_Player`) x `PlayerBaseStatMultipliers`, where a configured `BaseStats` value is
already the level-1 value and is not multiplied again. A `Game.ini` multiplier that is not set, or set
to 0, counts as 1, as it does in the game.
Everything is computed from the character class defaults and the server's `PlayerBaseStatMultipliers` and
`PerLevelStatsMultiplier_Player`, never from the live values, so applying again cannot stack on itself
and a character never keeps a perk after the group that gave it expires. Pool stats (health, stamina,
food, water, oxygen) keep the same fill ratio when their maximum changes.

## Build

Requirements: Visual Studio 2026 with the "Desktop development with C++" workload (v145 toolset), and Git.

AsaApi is a git submodule at `extern\AsaApi`. After cloning the repository:

```
git submodule update --init --recursive
```

1. Build `extern\AsaApi\AsaApi.sln` (**Release / x64**) so `AsaApi.lib` lands in `extern\AsaApi\out_lib\`,
   or copy `AsaApi.lib` there from the `Lib` folder of the AsaApi release zip your servers run.
2. Open `PlayerStatPerks.sln`, select **Release / x64**, build.

`fmt` comes from vcpkg through the manifest in `vcpkg.json` (Visual Studio's bundled vcpkg; triplet
`x64-windows-static-md`). AsaApi's `UnrealString.h` includes `fmt/format.h`, so it has to be on the include
path. Permissions is not linked at build time: the plugin looks its functions up in the loaded
`Permissions.dll` at runtime (`src\PermissionsApi.h`), so no Permissions import library is needed.

The result is `out\PlayerStatPerks\`, containing `PlayerStatPerks.dll`, its `.pdb`, `PluginInfo.json`,
`config.json`, `config.commented.json` and `THIRD-PARTY-NOTICES.md`. Keep the notices file with the DLL
when redistributing the plugin.

Build against the AsaApi version your servers run. To move to a different AsaApi release, check out that
tag in the submodule and commit the new submodule pointer:

```
cd extern\AsaApi
git fetch --tags
git checkout <tag>
cd ..\..
git add extern\AsaApi
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
   `Applied [Default, ...] to <EOS ID>` as players join.

To update a running server, drop the new build in as `PlayerStatPerks.dll.arkapi` (the build produces that
copy too). If automatic plugin reloading is enabled in AsaApi's own `config.json`, AsaApi swaps it in and
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
| `GroupRecheck_Seconds` | `10` | How often each player's groups are read again. This is the delay before a timed group expiring takes effect (a group being added is reported by Permissions and applies on the next poll). At least 1. |

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
- **`Permissions.dll is loaded but does not export ...`.** The installed Permissions build doesn't have
  the function under the name this plugin expects, so player stats are left alone. Compare with
  `dumpbin /exports Permissions.dll | findstr GetPlayerGroups` and update the name in `src\PermissionsApi.h`.
- **No `Applied` lines at all.** Check Permissions is installed in its own plugin folder and loads without
  errors; until it is loaded this plugin waits.
- **Stats don't change after a timed group is added.** `ArkApi.log` should show `Listening for
  Permissions group changes` once after startup; without it, changes are only seen on the recheck. Check
  `/perks` lists the group. If it doesn't, the player is not in that group as far as Permissions knows.
- **`Config: <group>: unknown multiplier/base stat '...' is ignored`.** A stat name is misspelled; the stat
  keeps its default for that group. The names are listed under Groups above.

## Tests

`tests\logic_tests.cpp` covers config parsing, warnings, group stacking, unit conversion and the
`/perks` text. It needs no game and no Windows. From a Visual Studio developer prompt in this folder:

```
cl /std:c++20 /EHsc /I extern\AsaApi\AsaApi\Core\Public /I src tests\logic_tests.cpp
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
| `src\PermissionsApi.h` | Runtime lookup of the Permissions functions this plugin uses (`GetPlayerGroups` and the group-change notification). |
| `configs\config.json` | Default configuration. |
| `configs\config.commented.json` | The same configuration with every setting explained. Not read by the plugin. |
| `..\THIRD-PARTY-NOTICES.md` | License notice for the nlohmann/json code compiled into the DLL. Copied into the build output. |
| `configs\PluginInfo.json` | Plugin metadata read by AsaApi. |
| `tests\logic_tests.cpp` | Stand-alone tests for `PerkLogic.h` and the shipped config. |
| `vcpkg.json` | vcpkg manifest (fmt). |
| `extern\AsaApi` | AsaApi submodule the plugin builds against. |
