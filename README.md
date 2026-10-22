```
                   _____                _                      
  ,:\      /:.    /  __ \              | |                  
 //  \_()_/  \\   | /  \/_ __ ___  __ _| |_ _   _ _ __ ___  
||   |    |   ||  | |   | '__/ _ \/ _` | __| | | | '__/ _ \ 
||   |    |   ||  | \__/\ | |  __/ (_| | |_| |_| | | |  __/  
||   |____|   ||   \____/_|  \___|\__,_|\__|\__,_|_|  \___|  
 \\  / || \  //    _____                 _           _ 
  `:/  ||  \;'    /  __ \               | |         | |  
       ||         | /  \/ ___  _ __ ___ | |__   __ _| |_ 
       ||         | |    / _ \| '_ ` _ \| '_ \ / _` | __|
       XX         | \__/\ (_) | | | | | | |_) | (_| | |_
       XX           \____/\___/|_| |_| |_|_.__/ \__,_|\__|
       XX         ______                           _             __________
       XX         | ___ \                         | |           /\____;;___\
       OO         | |_/ /_____      ____ _ _ __ __| |___       | /         /
       `'         |    // _ \ \ /\ / / _` | '__/ _` / __|      `. ())oo() .
                  | |\ \  __/\ V  V / (_| | | | (_| \__ \     |\(%()*^^()^\
                  \_| \_\___| \_/\_/ \__,_|_|  \__,_|___/    %| |-%-------|
                                                            % \ | %  ))   |
                                               ejm97  %%%%  %  \|%________|
 ```

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
