# CreatureCombatRewards

An [AsaApi](https://github.com/ArkServerApi/AsaApi) plugin for ARK: Survival Ascended servers. Players earn
ArkShop points for killing wild creatures. How much a kill pays depends on the **creature type** it falls
under (for example *Aggressive* or *Threatening*) and the creature's **level**. Passive creatures can be
excluded so they never pay.

## How it decides what a kill pays

1. Only **wild** creatures count. Tamed, claimed and unclaimed-tame creatures never pay.
2. The kill must be credited to a **player**: on foot or riding a tame. Kills by turrets, traps, plants and
   wild creatures pay nothing. Kills by unridden tames pay nothing unless you turn on
   `RewardUnriddenTameKills`.
3. The creature is matched against `Excluded` and against each entry in `CreatureTypes`:
   - `Excluded` match: no points.
   - `CreatureTypes` match: that type's `LevelRanges` decide the points.
   - No match anywhere: the `Fallback` section decides, by diet. With the shipped config an unlisted
     carnivore counts as *Aggressive* and an unlisted herbivore pays nothing.
4. The points for the creature's level are multiplied by `PointsMultiplier` and queued. Queued points are
   handed to ArkShop every `PayoutIntervalSeconds`.

## Build

Requirements: Visual Studio 2022 or 2026 with the "Desktop development with C++" workload, and Git.
The project uses whichever compiler toolset your Visual Studio ships with (v143 on 2022, v145 on 2026),
so there is nothing to retarget.

1. Run `setup.bat`. It clones the AsaApi source into `extern\AsaApi`.
2. Copy `AsaApi.lib` into `extern\AsaApi\out_lib\`. It is in the `Lib` folder of the AsaApi release zip
   (the same release you run on your servers). Building `extern\AsaApi\AsaApi.sln` yourself also produces it.
3. Open `CreatureCombatRewards.sln`, select **Release / x64**, build.

The result is `out\CreatureCombatRewards\`, containing `CreatureCombatRewards.dll`, `PluginInfo.json` and `config.json`.

No vcpkg packages and no ArkShop import library are needed. ArkShop's `AddPoints` function is looked up
in the loaded `ArkShop.dll` at runtime. AsaApi's headers ask for an `fmt/format.h`; `compat\fmt\format.h`
answers that by pointing at the fmt copy AsaApi already bundles.

Build against the AsaApi version your servers run. If the AsaApi source or `AsaApi.lib` live somewhere
else, pass `/p:AsaApiDir=...` and `/p:AsaApiLib=...` to msbuild.

## Install

The server machine needs a Microsoft Visual C++ x64 Redistributable at least as new as the Visual Studio
the plugin was built with. A machine with that Visual Studio installed already has it; otherwise install
the latest one from Microsoft.

1. Copy the `out\CreatureCombatRewards` folder to `ShooterGame\Binaries\Win64\ArkApi\Plugins\` on each server.
   The folder and the DLL must keep the same name.
2. ArkShop must be installed and working on that server.
3. Start the server. `ArkApi.log` shows `Config loaded: ...` when the plugin is ready.

To update a running server, drop the new build in as `CreatureCombatRewards.dll.arkapi` (the build produces
that copy too). If automatic plugin reloading is enabled in AsaApi's own `config.json`, AsaApi swaps it
in, and queued points are paid out before the old version unloads.

## config.json

Comments (`//` and `/* */`) are allowed in this file.

`configs\CommentedConfig.json` holds the same settings and values as `config.json`, with an explanation
on every setting and the in-game name beside every creature entry. It is a reference copy: the plugin
only reads `config.json`. Because comments are allowed, you can also copy it over `config.json` to keep
the notes on the server.

### General

| Setting | Default | Meaning |
|---|---|---|
| `PayoutIntervalSeconds` | `30` | How often queued points are handed to ArkShop. `0` pays on every kill. |
| `PointsMultiplier` | `1.0` | Multiplies every payout (rounded to the nearest whole point). `2.0` for a double-points weekend. |
| `RewardUnriddenTameKills` | `false` | If `true`, a kill by an unridden tame is credited to the nearest living tribe member within `TameKillMaxDistance`. |
| `TameKillMaxDistance` | `15000` | Range for the setting above, in Unreal units (one foundation is 300, so 15000 is about 50 foundations). |
| `LogKills` | `false` | Writes one line to `ArkApi.log` for every wild creature a player kills, including kills that pay nothing. Use it to tune the lists. |

Batching payouts keeps a busy fight from becoming dozens of database writes and chat lines, because
ArkShop writes to its database and sends its own "you received points" chat message on every payout.
The trade-off: points still queued when a server crashes or is killed are lost (at most one interval's
worth). A normal plugin unload pays them first.

### Notifications

The message a player sees at the moment of the kill. ArkShop's own message follows at the next payout.

| Setting | Default | Meaning |
|---|---|---|
| `Enabled` | `true` | Show a message for each paying kill. |
| `UseChat` | `false` | `false`: on-screen notification. `true`: chat message from `Sender`. |
| `Sender` | `Combat Rewards` | Sender name for chat messages. |
| `Message` | `+{points} points - {creature} (Lvl {level})` | Placeholders: `{points}`, `{creature}`, `{level}`, `{type}`. |
| `DisplayTime` | `4.0` | Seconds an on-screen notification stays up. |
| `TextScale` | `1.2` | Size of an on-screen notification. |

### CreatureTypes

A list of types. You can rename them, add more, or remove them.

```json
{
  "Name": "Aggressive",
  "Creatures": [ "Chalico_Character_BP", "*_Character_BP_Corrupt" ],
  "LevelRanges": [
    { "MinLevel": 1,   "MaxLevel": 59, "Points": 1 },
    { "MinLevel": 60,  "MaxLevel": 119, "Points": 3 },
    { "MinLevel": 120, "Points": 5 }
  ]
}
```

- `LevelRanges`: the first range containing the creature's level sets the points. Leave `MaxLevel` out for
  "this level and above". A level that no range covers pays nothing.
- `Creatures`: which creatures belong to the type. See the matching rules below.

The shipped config defines four types:

| Type | Holds | Pays |
|---|---|---|
| `Threatening` | Apex predators, alphas, wyverns, Rock Drakes and Reapers, plus the Therizinosaur and Dreadnoughtus. | 3 to 30, by level |
| `Aggressive` | Corrupted creatures, Chalicotherium, Gigantopithecus and Megatherium, plus every unlisted carnivore through `Fallback`. | 1 to 6, by level |
| `Defensive` | Herbivores that leave players alone until they are hit, such as Triceratops, Stegosaurus and Brontosaurus. | 1 to 3, by level |
| `Colossal` | Titanosaur. | 50, at any level |

The shipped level ranges are built for a wild level cap of 300: twelve 30-level steps, the last of which
covers level 330 and above. Creatures that spawn over the cap (tek creatures, wyverns, Rock Drakes, and
cave and OSD spawns) all land in that last step, so they pay no more than a creature just above the cap.
For a different cap, rescale the `MinLevel` and `MaxLevel` values.

The Titanosaur gets one flat payout because wild ones spawn at a very low level and their health and
damage do not grow with it, so paying by level would give it almost nothing.

### Excluded

Creatures that never pay, written the same way as `Creatures`. The shipped list holds fish, small passive
animals, skittish flyers, and bees (hives respawn drones without end).

### Fallback

What to do with a creature that is in neither `Excluded` nor any type's `Creatures`.

| Setting | Shipped value | Meaning |
|---|---|---|
| `Carnivores` | `"Aggressive"` | Type used for unlisted creatures the game flags as carnivores. `""` means no reward. |
| `Herbivores` | `""` | Type used for unlisted creatures not flagged as carnivores. |

This is what lets modded predators pay without being listed. Diet is only a rough stand-in for
aggression: some carnivores are passive (fish, otters, Pteranodons), which is what `Excluded` is for, and
some herbivores attack on sight (Therizinosaur, Chalicotherium) or fight back when hit (Triceratops,
Stegosaurus), which is why those are listed explicitly. For a strict whitelist, set both values to `""`.

### How creatures are matched

Each entry in `Creatures` and `Excluded` is one of:

| You write | It matches |
|---|---|
| `Rex_Character_BP` | A blueprint class name (the `_C` suffix is optional), **including every blueprint that inherits from it**. This one entry covers Tek Rex and modded Rex variants built on the vanilla blueprint. |
| `Rex` | A creature name tag, the same tags `Game.ini` uses. |
| `*_Character_BP_Corrupt` | A wildcard on the class name or name tag. `*` is any run of characters, `?` is one. |
| `/Game/Mods/SomeMod/*` | Anything containing `/` is matched against the blueprint path. A full `Blueprint'/Game/...'` string works too. |

Matching ignores upper and lower case.

**The most specific match wins.** The creature's own class is checked first, then its parent blueprint,
then the grandparent, and so on. So:

- `MegaRex_Character_BP` under *Threatening* beats `Rex_Character_BP` under any other type, whatever
  order the types are in.
- A corrupted Pteranodon (`Ptero_Character_BP_Corrupt`) pays as *Aggressive* through the wildcard on its
  own class, even though its parent `Ptero_Character_BP` is excluded.
- If one class matches several lists, `Excluded` wins, then the types in the order they appear.

### Finding the right names

Set `LogKills` to `true`, reload, and kill the creature. `ArkApi.log` shows exactly what the plugin saw:

```
Kill by 0002a1b2c3...: "Rex" class=Rex_Character_BP_C parent=Dino_Character_BP_C tag=Rex level=145 diet=carnivore -> type=Threatening (listed: Rex_Character_BP) points=13
Kill by 0002a1b2c3...: "Coelacanth" class=Coel_Character_BP_C parent=Dino_Character_BP_C tag=Coel level=20 diet=carnivore -> type=- (excluded: Coel_Character_BP) points=0
Kill by 0002a1b2c3...: "Raptor" class=Raptor_Character_BP_C parent=Dino_Character_BP_C tag=Raptor level=35 diet=carnivore -> type=Aggressive (fallback) points=1
Kill by 0002a1b2c3...: "Parasaur" class=Para_Character_BP_C parent=Dino_Character_BP_C tag=Para level=50 diet=herbivore -> type=- (unlisted) points=0
```

`fallback` lines are the ones to review: they are creatures the config does not mention. `unlisted` lines
are creatures the config does not mention and the fallback does not pay for; add one to a type's
`Creatures` if it should pay.

## Commands

| Command | Where | What it does |
|---|---|---|
| `CreatureCombatRewards.Reload` | RCON, or the in-game console as an admin (`cheat CreatureCombatRewards.Reload`) | Re-reads `config.json`. If the file has a mistake the old settings stay active and the error is reported. |

## Troubleshooting

- **`Config not loaded: ...` at startup.** The message names the setting that is wrong. Fix the file and
  run `CreatureCombatRewards.Reload`. Until then no kill pays.
- **`ArkShop is not loaded; N point(s) are waiting`.** ArkShop is missing or failed to load. Points stay
  queued and are paid once ArkShop is available.
- **`ArkShop is loaded but does not export ?AddPoints@...`.** The installed ArkShop build does not expose
  the points function under the name the official build uses. Check with
  `dumpbin /exports ArkShop.dll | findstr AddPoints`.
- **`Could not hook APrimalDinoCharacter.Die...`.** The AsaApi on the server does not know that function,
  usually because the API is out of date for the current game version.

## Tests

`tests\logic_tests.cpp` covers config parsing, matching, level ranges and the payout queue. It needs no
game and no Windows. From a Visual Studio developer prompt in this folder:

```
cl /std:c++20 /EHsc /I extern\AsaApi\AsaApi\Core\Public /I src tests\logic_tests.cpp
logic_tests.exe configs\config.json
```

Passing a config file validates it and prints any warnings, which is a quick way to check an edit before
putting it on a server.

Add `--shipped` after the path to also check the file against the payouts expected from the shipped
config. Run it on both shipped files after changing either, since they must hold the same values:

```
logic_tests.exe configs\config.json --shipped
logic_tests.exe configs\CommentedConfig.json --shipped
```

## Files

| File | Purpose |
|---|---|
| `src\Main.cpp` | Death hook, player crediting, notifications, payouts, commands. |
| `src\RewardLogic.h` | Config parsing, creature matching, level ranges, payout queue. No game dependencies. |
| `src\ShopBridge.h` | Runtime lookup of ArkShop's `AddPoints`. |
| `compat\fmt\format.h` | Lets AsaApi's headers compile without installing fmt through vcpkg. |
| `configs\config.json` | Default configuration. |
| `configs\CommentedConfig.json` | The same configuration with every setting and creature entry explained. Not read by the plugin. |
| `configs\PluginInfo.json` | Plugin metadata read by AsaApi. |
| `tests\logic_tests.cpp` | Stand-alone tests for `RewardLogic.h` and the shipped config. |
