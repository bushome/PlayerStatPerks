```
______ _
| ___ \ |
| |_/ / | __ _ _   _  ___ _ __
|  __/| |/ _` | | | |/ _ \ '__|
| |   | | (_| | |_| |  __/ |
\_|   |_|\__,_|\__, |\___|_|
                __/ |
               |___/
 _____ _        _
/  ___| |      | |
\ `--.| |_ __ _| |_
 `--. \ __/ _` | __|
/\__/ / || (_| | |_                                   /\
\____/ \__\__,_|\__|              ___                /  \
______          _                |   |        ___   /_  _\
| ___ \        | |            ___|   |  ___  |   |    ||
| |_/ /___ _ __| | __ ___    |   |   | |   | |   |  _ || _
|  __// _ \ '__| |/ // __|   |   |   | |   | |   |  \ || /
| |  |  __/ |  |   < \__ \   |   |   | |   | |   |   \  /
\_|   \___|_|  |_|\_\|___/   |___|___| |___| |___|    \/
```

Server plugin that sets player base stats and per-level stat gains by Permissions group. Groups can be
permanent, tribe or timed groups; a timed group's perks start and stop on their own.

| Folder | Game | Server API (submodule) |
|---|---|---|
| [`ASA`](ASA/README.md) | ARK: Survival Ascended | [AsaApi](https://github.com/ArkServerApi/AsaApi) at `ASA/extern/AsaApi` |
| [`ASE`](ASE/README.md) | ARK: Survival Evolved | [ArkServerApi (AseApi)](https://github.com/ArkServerApi/AseApi) at `ASE/extern/AseApi` |

Each folder is a self-contained Visual Studio solution. Clone with submodules:

```
git clone --recurse-submodules https://github.com/bushome/PlayerStatPerks.git
```

or, in an existing clone:

```
git submodule update --init --recursive
```

The perk logic (`src/PerkLogic.h`), the tests and the configs are the same in both folders; only
`src/Main.cpp`, `src/PermissionsApi.h`, `lib/Permissions.lib` and the project files differ by API. One
behavioural difference: the ASA Permissions plugin reports group changes, so a newly added group applies
within a couple of seconds there; the ASE config instead re-reads groups on every poll for the same result. Build and install steps
are in each folder's README.
