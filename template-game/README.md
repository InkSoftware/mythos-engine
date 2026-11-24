# template-game

A small, complete game on mythos-engine: the starting point for a new one. Copy this folder, rename it and
change it.

You walk a two-room hall in the first person, open a door, and a guide (the engine's CC0 stand-in
character, a skinned glTF model) patrols the first room, stopping to face you when she sees you. Entering the back
room fires a level trigger whose rule shows a line of text. F5 saves, F9 loads.

```
WASD walk   mouse look   Shift run   E use a door   F5 save   F9 load   Esc quit
```

## Build and run

It builds with the engine (`MYTHOS_BUILD_TEMPLATE_GAME`, on when the engine is the top-level project):

```sh
cmake -S .. -B ../build && cmake --build ../build --parallel
../build/template-game/template_game          # on macOS: template_game.app/Contents/MacOS/template_game
ctest --test-dir ../build                     # the engine's tests and this game's
../build/template-game/template_game --screenshot shot.ppm   # renders 90 frames, saves one, exits
```

Standalone, from this folder: `cmake -S . -B build` (it takes the engine from the folder above;
`-DMYTHOS_ENGINE_DIR=...` names another checkout). In a copy outside the engine, replace the `FetchContent_Declare`
in `CMakeLists.txt` with a `GIT_REPOSITORY` and tag, as the engine's README shows.

## What is in it

| File | What it shows |
| --- | --- |
| `src/game.h`, `src/game.cpp` | The rules, with no SDL or GL (so they are tested headless): a `Level` from `assets/levels/hall.txt` and the tile legend `assets/data/tiles.json`, the `Player` and `PlayerWalker`, `RoomDoors` and using a door, the trigger and rule runtime (`EventGraphRuntime`) with the game's own action types (`set_flag`, `message`), a behaviour tree (`ai::`) with `CanSee` and A* patrol (`FindPath`) driving an `AnimStateMachine` (idle, walk, and a wave that plays once when she first sees you), and saving (`SaveSlots`, `PlayerToJson`, `ProgressToJson`). |
| `src/main.cpp` | The front end: window, textures (`AddCommonMaterials`, model textures), `SceneRenderer`, input (`PollInput`), the camera and lamps each frame, door panels, the skinned model instance (`ModelInstance::skin`) and a little text on the overlay. |
| `assets/` | `levels/hall.txt` (`[map]`, `[furniture]`, `[triggers]`, `[rules]`), `data/tiles.json`, `data/en.json` and `el.json` (strings, looked up with `Tr`). The engine's own assets (models, textures, the character) are laid under it. |
| `tests/game_test.cpp` | Headless tests of the rules: loading, walls, a door and its trigger, the patrol, saving. |

## Making it yours

1. Rename the project and the targets in `CMakeLists.txt`; keep `template_game_rules` (logic) apart from the
   executable (SDL), so the rules stay testable.
2. Edit the level with the engine's editor (`MythosEditor --project <this folder>`): the map, furniture, triggers and rules.
3. Put your own models (OBJ) in `assets/models/`, textures (PNG, BMP) in `assets/` named after the texture
   they replace; a file of the same name wins over the engine's.
4. Replace the `message` and `set_flag` actions in `Game::RunEvents` with your own, and the guide's tree in `Game::Restart`.
5. Add settings (`GameSettings`, `DrawSettingsPage`), sound (`LoadSoundBank`, `AudioEngine`) and menus from the engine
   as you need them; the engine's README lists what each gives.
6. To ship, pack the assets with `mythos_pack` (or copy them beside the executable) instead of the development paths
   compiled in by `CMakeLists.txt` (`TEMPLATE_ENGINE_ASSETS`, `TEMPLATE_GAME_ASSETS`).

The template is kept in step with the engine: a change to the engine's API that this game uses must update it too (see the
engine's `CLAUDE.md`).
