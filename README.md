# CreatureCombatRewards

Server plugin that awards ArkShop points for killing wild creatures, by creature type and level range.
Passive creatures can be excluded so they never pay.

| Folder | Game | Server API (submodule) |
|---|---|---|
| [`ASA`](ASA/README.md) | ARK: Survival Ascended | [AsaApi](https://github.com/ArkServerApi/AsaApi) at `ASA/extern/AsaApi` |
| [`ASE`](ASE/README.md) | ARK: Survival Evolved | [ArkServerApi (AseApi)](https://github.com/ArkServerApi/AseApi) at `ASE/extern/AseApi` |

Each folder is a self-contained Visual Studio solution. Clone with submodules:

```
git clone --recurse-submodules https://github.com/bushome/CreatureCombatRewards.git
```

or, in an existing clone:

```
git submodule update --init --recursive
```

The reward logic (`src/RewardLogic.h`), the tests and the configs are the same in both folders; only
`src/Main.cpp`, `src/ShopBridge.h` and the project files differ by API. Build and install steps are in
each folder's README.
