# template-game

A small, complete game on mythos-engine: the starting point for a new one. Copy this folder, rename it and
change it.

You walk a two-room hall in the first person and open a door. A guide (the engine's CC0 stand-in character, a skinned
glTF model) patrols the first room and stops to face you, with a wave, when she sees you. Entering the back room fires a
level trigger whose rule shows a line of text. The game opens with a short cutscene (E skips it).

```
WASD walk   mouse look   Shift run   E use a door   Esc settings   F3 controls   F5 save   F9 load   (close the window to quit)
```

## Build and run

It builds with the engine (`MYTHOS_BUILD_TEMPLATE_GAME`, on when the engine is the top-level project):

```sh
cmake -S .. -B ../build && cmake --build ../build --parallel
../build/template-game/template_game          # on macOS: template_game.app/Contents/MacOS/template_game
ctest --test-dir ../build                     # the engine's tests and this game's
```

Standalone, from this folder: `cmake -S . -B build` (it takes the engine from the folder above;
`-DMYTHOS_ENGINE_DIR=...` names another checkout). In a copy outside the engine, replace the `FetchContent_Declare`
in `CMakeLists.txt` with a `GIT_REPOSITORY` and tag, as the engine's README shows.

### Flags

Each flag shows one engine feature. (zsh does not split a quoted string into words: pass each flag as its own word.)

| Flag | What it does | The engine side |
| --- | --- | --- |
| `--no-intro` | leaves out the opening cutscene | `CutscenePlayer` |
| `--menu`, `--controls` | opens the settings menu, or its controls page, at the start | `UpdateSettingsPage`, `UpdateControlsPage` |
| `--outdoor [--hour H]` | no roof, and a day that runs (ten seconds an hour) | `CameraState::outdoor`, `time_of_day` |
| `--sun-shadow` | with `--outdoor`: the shadow of the sun or moon | `CameraState::sun_shadow` |
| `--ssao` | ambient occlusion (also the Ambient occlusion setting) | `CameraState::ssao` |
| `--baked` | the lamps baked into light probes instead of live lamps | `BakeProbes`, `UploadProbes` |
| `--reflections` | a reflection probe in each room, with the room's box for parallax correction (the toolbox and bucket are shiny; the two nearest blend as you cross between rooms) | `AddReflectionProbe` |
| `--crowd N` | N chairs as model instances; `--no-instancing` draws them one by one | GPU instancing |
| `--debug` | opens the debug console (the ` key or F12 anywhere) with the frame stats and graph on: every setting is a variable, `cutscene.play intro` runs a cutscene, `help` lists it all | `CvarRegistry`, `DebugConsole`, `DrawDebugOverlay`, `RegisterCutsceneCommands` |
| `--screenshot file.ppm` | renders 90 frames, saves the last, prints the draw calls, exits | `WriteFramebufferPpm`, `RenderStats` |

## What is in it

| File | What it shows |
| --- | --- |
| `src/game.h`, `src/game.cpp` | **The rules**, with no SDL or GL, so they are tested headless: a `Level` from `assets/levels/hall.txt` and the tile legend `assets/data/tiles.json`; the `Player` and `PlayerWalker`; `RoomDoors`; the trigger and rule runtime (`EventGraphRuntime`) with the game's own action types (`Game::Act`: `set_flag`, `message`), also used by the cutscene's cues; the guide: a behaviour tree (`ai::`) that greets you when she sees you (`CanSee`), searches where she last saw you (`LastSeen`), goes to look at a noise such as a door (`NoiseBoard`) and otherwise patrols, walking smoothed A* routes (`FindPath`, `SmoothPath`, `SteerAround` around you), driving an `AnimStateMachine` (idle, walk, and a wave that plays once when she first sees you); saving (`SaveSlots`, `PlayerToJson`, `ProgressToJson`, `LevelDiffToJson` for the doors, `FurnitureDiffToJson` for furniture taken or put down, the guide as its own section); `ReloadLevel` and `ReloadModel` for hot reload; the sound cues (`TakeSounds`: footsteps, doors) that the front end plays. |
| `src/main.cpp` | **The front end** (and the debug console: `RegisterSettingsCvars`, `RegisterDebugCvars`, `DebugConsole`): the window, textures (`AddCommonMaterials`, the models' own), `SceneRenderer`, the frame loop. Input: `PollInput`, then `ActionTracker` and `ApplyActions` so keys are rebindable; Esc opens the settings menu (`UpdateSettingsPage` / `DrawSettingsPage`, loaded from and saved to `settings.txt`, applied with `ApplySettings`) and F3 or its Controls row the controls page (`UpdateControlsPage` / `DrawControlsPage`, saving the `bindings` line of `settings.txt`); the game pauses meanwhile. Each frame: the camera and lamps, door panels, the skinned guide (`ModelInstance::skin`, `max_distance`), a camera shake with a slight tilt when a door slams (`CameraFx` with `max_roll`, `ApplyCameraFx`, `CameraState::roll`), a little text on the overlay. Sound: an ambience group that dips under effects (`AddGroupDuck`), a music layer that fades in in the back room (`StartMusic`, `SetMusicLayer`), a room reverb that follows the size of the room you stand in (`ReverbForRoom`, `AudioEngine::SetReverb`) with a share per sound (doors ring, footsteps stay dry: `Play(..., reverb)`), and a hum in the back room as a positional source (`PositionalGain`) muffled by the walls and the closed door (`CountSoundBlockers`, `ComputeOcclusion`, `SetSourceMuffle`). Hot reload: a `FileWatcher` over the game's assets and the engine's shaders, models and sounds; edit `assets/levels/hall.txt`, drop a `brick.png` in `assets/`, or change a shader, an `.obj` or a `.wav`, and the running game reloads it (a mistake is logged and the old one kept). |
| `assets/levels/hall.txt` | a level: `[map]`, `[furniture]`, `[triggers]`, `[rules]` |
| `assets/levels/hall.lamps`, `hall.probes` | the static lamps and the light probes the engine's `mythos_bake_probes` made from them (`--baked` loads the file; a ctest re-bakes it) |
| `assets/data/` | `tiles.json` (the tile legend), `en.json` and `el.json` (strings, looked up with `Tr`) |
| `assets/cutscenes/intro.cutscene` | the opening: a camera glide, a fade, a line of text, the guide's wave (`Game::StartCutscene`); the editor's View > Cutscene panel edits it |
| `assets/prefabs/dining.prefab` | a prefab: a table, two chairs and a bowl in the level's own line format; the editor's Inspector > Prefabs saves and places them |
| `tests/game_test.cpp` | headless tests of the rules: loading, walls, a door and its trigger, the patrol and the wave, the intro and skipping it, saving, a prefab placed into the hall |

The engine's own assets (models, textures, sounds, fonts, the character) are laid under this game's `assets/`; a file of
the same name here wins.

## Making it yours

1. Rename the project and the targets in `CMakeLists.txt`; keep `template_game_rules` (logic) apart from the
   executable (SDL), so the rules stay testable.
2. Edit the level with the engine's editor (`MythosEditor --project <this folder>`): the map, furniture, triggers, rules and
   prefabs. A release of the editor has the engine's assets beside it; a build from source does not, so name them:
   `MYTHOS_ENGINE_ASSET_DIR=<engine>/assets ../build/MythosEditor --project .` (on macOS the binary is
   `MythosEditor.app/Contents/MacOS/MythosEditor`).
3. Put your own models (OBJ) in `assets/models/` and textures (PNG, BMP) in `assets/` named after the texture they
   replace; a file of the same name wins over the engine's. A rigged, animated model is a `.glb` (or `.gltf`): the engine's
   `tools/mythos_models.py` keeps it with `Spec(skinned=True)`, and `LoadGlb` reads it as `Game::Load` does for the guide;
   its colour textures come with it (`GltfModel::textures`, which `main.cpp` registers).
4. Replace the `message` and `set_flag` actions in `Game::Act` with your own, and the guide's tree in `Game::Restart`.
5. Add what you need from the engine's README: more settings pages, a sound bank (`LoadSoundBank`), achievements,
   particles, a mirror, decals.
6. Drop the flags and the development paths you do not need. To ship, pack the assets with `mythos_pack` (or copy them
   beside the executable) instead of the paths compiled in by `CMakeLists.txt` (`TEMPLATE_ENGINE_ASSETS`,
   `TEMPLATE_ENGINE_SHADERS`, `TEMPLATE_GAME_ASSETS`), and leave out the file watcher.

The template is kept in step with the engine: a change to the engine's API that this game uses must update it too (see the
engine's `CLAUDE.md`).
