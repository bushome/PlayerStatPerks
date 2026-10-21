# PlayerStatPerks

Server plugin that sets player base stats and per-level stat gains by Permissions group. Groups can be
permanent, tribe or timed groups; a timed group's perks start and stop on their own.

| Folder | Game | Server API (submodule) |
|---|---|---|
| [`ASA`](ASA/README.md) | ARK: Survival Ascended | [AsaApi](https://github.com/ArkServerApi/AsaApi) at `ASA/extern/AsaApi` |
| `ASE` | ARK: Survival Evolved | Not ported yet. Will use [ArkServerApi (AseApi)](https://github.com/ArkServerApi/AseApi) at `ASE/extern/AseApi` |

Each folder is a self-contained Visual Studio solution. Clone with submodules:

```
git clone --recurse-submodules https://github.com/bushome/PlayerStatPerks.git
```

or, in an existing clone:

```
git submodule update --init --recursive
```

The perk logic (`src/PerkLogic.h`), the tests and the configs are meant to be the same in both folders;
only `src/Main.cpp`, `src/PermissionsApi.h` and the project files differ by API. Build and install steps
are in each folder's README.
