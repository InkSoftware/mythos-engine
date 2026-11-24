# mythos-engine

A small C++23 engine for first-person grid games on SDL3 and OpenGL 4.1 core, with no game engine
underneath. It carries games of one kind: a player walking a grid of rooms, doors, things to read,
listen to and pick up, menus, and a bit of procedural art.

- **World**: grid levels and tile legends, movement and pathfinding, line of sight, the player, room doors,
  props with verbs (`Interactor`, a prop carries the `floor` of the building it is on), a notebook, clues and
  flags (`Progress`). A tile can be **thin** (a wall a fifth of a metre thick through the middle of its cell, with
  a door and a window as thin as it: `TileInfo::thin`, `window`, `Level::BoxBlocked`), so houses can have real
  walls on the same one-metre grid; walls are `kWallHeight` (2.5 m) high. A game with several storeys keeps one
  level per floor and draws them stacked, `kFloorPitch` apart (`BuildSceneGeometry(level, models, outdoor, y)`,
  `AppendSceneGeometry`; tile flags `openCeiling` / `openFloor` leave a stairwell open between two floors, and a
  door panel carries its floor's height: `DoorInstance::y`). The engine has no stairs and no height for the player:
  walking up is the game's rule.
- **Rendering** (OpenGL 4.1 core): per-pixel lighting with up to 32 lamps a frame plus baked light probes for the static ones (`gfx/probes.h`) and a hand light (torch or lighter), shadows from
  distance cube maps (off to "ultra max"), MSAA or FXAA, motion blur, a lens effect with bloom (the Visual effects setting) and a camcorder (VHS tape) look with a REC / PLAY / battery display and timecode (the Camcorder setting, `CameraState::tape` / `tape_time`), ambient occlusion (a setting, off by default), light scattered in the air (lamp glow, a visible torch beam), a battery-failing tint for the hand light (`CameraState::torch_tint`), render scale from 300 to 2160
  lines, a night sky that becomes a day (`CameraState::time_of_day`: sun, dawn and dusk colours, clouds, haze), a flash of lightning (`CameraState::lightning` lights the world and the sky; with `lightning_zoned` only inside
  `lightning_zones`, the rooms that have a window), culled world
  geometry, cutscenes (`world/cutscene.h`: a timeline of camera, cues, values and animation), a distance cull and level of detail for model instances (`gfx/lod.h`), SDF text, and hot reload of shaders and textures in development (`FileWatcher`, `SceneRenderer::ReloadShaders` / `ReloadTextures`).
- **Life and logic** (headless, tested): skeletal animation (`gfx/skeleton.h`: bones, clips, blending, crossfade, `AnimStateMachine`;
  `gfx/gltf.h` loads a skinned `.glb`; the renderer skins on the GPU through `ModelInstance::skin`), a behaviour tree and
  perception (`world/ai.h`), A* (`world/nav.h`), triggers and event rules (`world/trigger.h`, `core/events.h`), versioned
  save files with slots (`data/save.h`, `world/save_state.h`) and rebindable input actions (`ui/input_map.h`, `ProbeBinding` / `ApplyActions` in `platform/input.h`) with a controls page to rebind them (`ui/controls_ui.h`).
- **Everything around a game**: a settings system (pages, drawing, saving, applying), localisation with built-in
  English and Greek strings, JSON loaders for sounds, translations, achievements and credits, positional audio with occlusion (`audio/spatial.h`), packed
  assets, and procedural textures.

| Target | Needs | Holds |
| --- | --- | --- |
| `mythos::engine` | the standard library, `third_party/stb`, nlohmann/json, GLM | levels, tiles, movement, the player, the body and the light in its hand, room doors, props and verbs, `Progress`, meshes, OBJ models, textures and common materials, SDF fonts and text layout, the overlay toolkit, menus and the settings pages, i18n and the built-in strings, JSON, achievements, credits, the sound bank, the virtual file system, matrices and the view frustum, audio and light arithmetic |
| `mythos::engine_platform` | `mythos::engine`, SDL3 | window (vsync, fullscreen), the GL 4.1 renderer (RAII GL wrappers, shadow cube maps, multisampled scene target, composite pass, SDF text), the audio mixer, keyboard, mouse and game controller input (`PollInput`: analog stick walking, rumble, light bar, last device used; stick maths in `world/gamepad.h`), furniture offsets (a `[furniture]` line may end with `offX offZ lift`), tinted blended glass panes (`CameraState::glass`), cutout model materials (`cutout_<texture>`: leaf cards, alpha-tested; `tools/mythos_models.py` `Spec(cutout=True)` bakes them), blended model materials (`blend<percent>_<texture>`: `Batch::opacity`, a second pass after the solid scene, no shadow) and ground mist (`CameraState::mist`, `mist_height`), user files, frame clock and frame pacer, menu pointer, `ApplySettings` |
| `mythos::sdl_main` | SDL3 | link into an executable so SDL is linked; include `<SDL3/SDL_main.h>` in the file defining `main` |
| `mythos_pack` | `mythos::engine` | a tool that packs a game's assets folder into one file for the virtual file system (see Tools) |
| `mythos_engine_tests` | `mythos::engine` | the unit tests (no SDL, no display) |

`mythos::engine` knows nothing about SDL, OpenGL or any game, so every rule built on it can be tested
headless.

## Starting a game

`template-game/` is a small complete game on the engine (walk, doors, a patrolling animated guide, triggers and rules,
saving) with its own README: copy it to start a new one. It is built and tested with the engine.

## Layout

```
src/engine/   the engine, by kind, included as "engine/<folder>/<file>.h"
assets/       fonts, common sounds and textures, and the basic house and office models, that games share
tools/        mythos_assets.py, generate_models.py, mythos_models.py, fetch_cc0_models.py, mythos_pbr_maps.py, editor/ (MythosEditor), pack/ (mythos_pack)
tests/        test_main.cpp
template-game/  a small game to copy when starting a new one
third_party/  stb_truetype, stb_image
```

The sources are in folders by kind: `core/` (math, matrices, JSON, files, packed assets), `world/` (levels, tiles,
movement, the player, doors, props, the body), `audio/` (sound arithmetic, the sound bank), `data/` (achievements, credits,
translations), `gfx/` (surfaces, textures and their derived normal maps (or authored `_nrm` / `_rough` companions, light PBR), meshes, models, fonts and text, light arithmetic), `ui/` (overlay, paging and wrapping, menus,
settings), `editor/` (the level editor's rules: level files, projects, undo, prefabs, the game adapter) and `platform/` (window,
input, mixer; `platform/gl/` is the renderer, `platform/editor/` the editor's window and panels). A folder depends only
on the ones before it in that list (`gfx/` also on `world/`), apart from `platform/`, which uses them all.

## Using it

```cmake
include(FetchContent)
FetchContent_Declare(mythos_engine GIT_REPOSITORY <url> GIT_TAG v1.0.0-alpha.2)
FetchContent_MakeAvailable(mythos_engine)

add_executable(my_game ${MYTHOS_EXE_KIND} src/main.cpp)   # WIN32 / MACOSX_BUNDLE / plain, set by the engine
target_link_libraries(my_game PRIVATE my_game_rules mythos::engine_platform mythos::sdl_main)
target_link_libraries(my_game_rules PUBLIC mythos::engine)
```

To work on the engine and a game together, point FetchContent at your checkout:
`cmake -DFETCHCONTENT_SOURCE_DIR_MYTHOS_ENGINE=/path/to/mythos-engine ...`

Options: `MYTHOS_BUILD_PLATFORM` (SDL layer, default ON), `MYTHOS_BUILD_TESTS` (default ON when top level), `MYTHOS_WARNINGS_AS_ERRORS` (warnings in the engine's own code fail the build, default OFF, on in CI),
`MYTHOS_FETCH_SDL` (download SDL when none is installed, default ON), `MYTHOS_SDL_VERSION` (the SDL release fetched,
3.2.28), `MYTHOS_PORTABLE` (link SDL statically, always fetched, so the app runs without SDL installed),
`MYTHOS_LTO` (link-time optimization for Release, on by default). MSVC builds use `/utf-8`, which games inherit.

Targets the releases each vendor still supports: Ubuntu 22.04+ (GCC 11+), macOS 15+ and Windows 11 (MSVC 2022), with
CMake 3.22+, hence C++23 (GCC 11 accepts it as `-std=c++2b`). On macOS `CMAKE_OSX_DEPLOYMENT_TARGET` defaults to 15.0 when the engine is the top-level project
(a game sets its own before its `project()`), and `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` builds a universal binary (needs
`MYTHOS_PORTABLE`, since an installed SDL such as Homebrew's is usually arm64 only). Windows 10 is out of support, but nothing
in the code needs 11. CI builds and tests the oldest supported release of each OS (a binary built there runs on newer ones),
each with `MYTHOS_PORTABLE` on and off, plus the x86_64 slice under Rosetta (`.github/workflows/ci.yml`). SDL3 is fetched and built from source when no
installed copy is found.

Dependencies: SDL3 (3.2.28), [nlohmann/json](https://github.com/nlohmann/json) 3.12.0 and [GLM](https://github.com/g-truc/glm)
1.0.3, each used from an installed copy when CMake finds one and downloaded otherwise (no other setup), plus
`stb_truetype` and `stb_image`, which are bundled in `third_party/stb`. nlohmann/json is part of the engine's public
headers (`json::Value` is an `nlohmann::ordered_json`), so games get it through `mythos::engine`; GLM stays private.

OpenGL 4.1 core is the highest version macOS offers, so the renderer targets it on every platform.

## What a game supplies

The engine gives mechanisms; a game gives meaning.

- **Rules**: a class like `Game` that owns a `Level`, a `Player`, a `PlayerWalker`, `RoomDoors`, an
  `Interactor` and a `Progress`-derived state, and turns `Use` outcomes into text, sounds and plot.
- **Data**: levels (`[map]` + `[furniture]` (a line may end with `offX offZ lift`), and `[decor]` for pieces that block nothing), a tile legend, props, sounds, translations.
  The engine reads the shared formats for you: `LoadSoundBank`, `LoadTranslation`, `LoadAchievementDefs`,
  `LoadCredits`, `LoadPlayerParams`, `LoadFontFiles`, `ParseTileSet`.
- **Settings**: `GameSettings` with its pages (`UpdateSettingsPage`), `DrawSettingsPage` (give it a `MenuTheme`),
  and `platform/apply_settings.h` (`LoadSettings`, `SaveSettings`, `ApplySettings`). Their English and Greek
  strings are built in (`engine_strings.cpp`); the game's own tables override any of them.
- **Assets**: `assets/` holds fonts, common sounds (steps, doors, torch, battery, tick, static, wind) and eight
  common textures. A game merges it under its own assets (the game's file of the same name wins);
  `MYTHOS_ENGINE_ASSET_DIR` names the folder for CMake.
- **Art**: `AddCommonMaterials()` first, then its own textures (PNG, BMP, TGA or JPG files named after the texture
  override the built-in ones); models as OBJ (`tools/mythos_assets.py`
  builds them and the level files from Python).
- **A front end**: build the camera and lights each frame (`CameraState`, then `ApplySettings(settings, camera)`),
  poll input with `PollInput`, play cues with `PlayCue`, upload geometry to `SceneRenderer` (and models for things that move, with `UploadModel`; `CameraState::instances` and `CameraState::mirror` draw them and one wall mirror).

Levels may also contain `[triggers]` and `[rules]`. A game can load them with
`EventGraphRuntime::Load(level)`, call `Sample(position, floor, use_pressed, flags, callback)`
each update, and call `Dispatch(event, flags, callback)` for its own flag and timer events.
The callback implements action types; the game owns flags, timers and use-press edges.
The runtime tracks one point subject, samples discretely, and must be reloaded after level edits.
The level-file rule encoding is documented in `engine/core/events.h`.

## Models and textures

**Shipped in `assets/` (Poly Haven, CC0).** The basic models of houses and offices: furniture, household things,
picture frames, a torch and a lighter, a plant, boots, a hat, an oil lamp, a street lamp, a mason's plane, mallet and measuring tape, a cake and a rubber duck, plus common dressing (cupboards, chests, stools, crates, ladders, cushions, a tall clock, curtains, a rug, lamps, a workbench, shelving, a bench) of which the last few are procedural placeholders (`tools/generate_models.py`). They are OBJ files in `assets/models/`; each material is a texture named
`m_<model>_<n>` in `assets/modeltex/` (a game registers every BMP in that folder as a texture), with the optional
`_nrm`, `_rough`, `_metal` and `_ao` companions the Materials setting reads (`gfx/pbr.h`). Metric (a world unit is a metre),
feet on the floor and centred on the origin, front facing +z. They are baked by `tools/fetch_cc0_models.py` (the list) through
`tools/mythos_models.py` (the conversion; a game's own fetch tool reuses it, with its own list and root). A game's
assets are laid over these, so a model or texture of the same name replaces the engine's. Sources, authors and licences are
in `CREDITS.md`; `assets/data/model_credits.json` is the same list for a game's credits page. Colour maps are 256 or 512 px.
The picture frames show the `painting` texture (a "NO ART YET" placeholder: a game supplies its own). Their glass
and any other see-through (BLEND) material is left out of the baked models unless a `Spec` asks for it; the renderer does blend
(`blend<percent>_<texture>` materials, a second pass) and cuts leaf cards (`cutout_<texture>`, alpha-tested).

| Model | Size w x h x d (m) | Triangles | Textures | PBR maps | What it is |
| --- | --- | --- | --- | --- | --- |
| `barrel` | 0.74 x 0.87 x 0.76 | 10820 | 1 baked | metal, nrm, rough | wine barrels |
| `bed` | 0.90 x 1.20 x 2.00 | 49990 | 1 baked | metal, nrm, rough | beds |
| `bookshelf` | 1.10 x 1.65 x 0.47 | 10106 | 1 baked | metal, nrm, rough | bookshelves |
| `boombox` | 0.58 x 0.38 x 0.15 | 10168 | 2 baked | metal, nrm, rough | a boombox |
| `bowl` | 0.31 x 0.09 x 0.31 | 14122 | 1 baked | nrm, rough | wooden bowls |
| `broom` | 0.49 x 1.45 x 0.11 | 14258 | 2 baked | metal, nrm, rough | brooms |
| `bucket` | 0.37 x 0.55 x 0.34 | 5116 | 1 baked | metal, nrm, rough | buckets |
| `cake` | 0.23 x 0.10 x 0.23 | 13790 | 1 baked | nrm, rough | a cake |
| `cardbox` | 0.39 x 0.34 x 0.52 | 16952 | 1 baked | nrm, rough | cardboard boxes |
| `chair` | 0.43 x 0.97 x 0.58 | 22013 | 1 baked | metal, nrm, rough | chairs |
| `duck` | 0.11 x 0.14 x 0.15 | 4288 | 1 baked | nrm, rough | a rubber duck |
| `dustpan` | 0.33 x 0.10 x 0.37 | 4332 | 1 baked | metal, nrm, rough | a dustpan |
| `frame_fancy` | 0.60 x 0.46 x 0.02 | 938 | 2 baked | nrm, rough | pictures on the walls |
| `frame_stand` | 0.20 x 0.25 x 0.10 | 1632 | 1 baked + `painting` | metal, nrm, rough | photographs on the tables |
| `frame_tall` | 0.59 x 0.84 x 0.02 | 2584 | 1 baked + `painting` | metal, nrm, rough | pictures on the walls |
| `frame_wide` | 0.75 x 0.50 x 0.03 | 3676 | 1 baked + `painting` | metal, nrm, rough | pictures on the walls |
| `gamepad` | 0.18 x 0.01 x 0.20 | 9546 | 1 baked | metal, nrm, rough | a game controller |
| `hammer` | 0.11 x 0.02 x 0.28 | 3617 | 1 baked | metal, nrm, rough | mallets |
| `handplane` | 0.33 x 0.20 x 0.08 | 8220 | 1 baked | metal, nrm, rough | a hand plane |
| `hat` | 0.32 x 0.12 x 0.35 | 10304 | 1 baked | metal, nrm, rough | a hat |
| `jug` | 0.28 x 0.21 x 0.17 | 5770 | 1 baked | metal, nrm, rough | jugs |
| `kettle` | 0.32 x 0.30 x 0.25 | 14838 | 2 baked | metal, nrm, rough | kettles |
| `lamppost` | 0.66 x 2.85 x 1.37 | 20338 | 3 baked | metal, nrm, rough | street lamps |
| `laptop` | 0.39 x 0.33 x 0.29 | 14450 | 2 baked | metal, nrm, rough | a laptop |
| `lighter` | 0.01 x 0.06 x 0.05 | 3366 | 2 baked | metal, nrm, rough | a lighter |
| `nightstand` | 0.50 x 0.62 x 0.51 | 470 | 1 baked | nrm, rough | bedside tables |
| `oillamp` | 0.13 x 0.28 x 0.13 | 5856 | 1 baked | metal, nrm, rough | oil lamps |
| `oven` | 0.50 x 0.86 x 0.65 | 13914 | 2 baked | metal, nrm, rough | ovens |
| `plant` | 0.47 x 0.75 x 0.52 | 8929 | 1 baked | nrm, rough | house plants |
| `shoes` | 0.19 x 0.26 x 0.62 | 56128 | 2 baked | nrm, rough | boots |
| `sofa` | 0.85 x 1.07 x 0.77 | 5626 | 1 baked | nrm, rough | armchairs and sofas |
| `table` | 0.83 x 0.78 x 0.83 | 4254 | 1 baked | metal, nrm, rough | tables |
| `tape` | 0.04 x 0.07 x 0.17 | 2868 | 1 baked | metal, nrm, rough | a measuring tape |
| `toolbox` | 0.40 x 0.30 x 0.27 | 14228 | 1 baked | metal, nrm, rough | toolboxes |
| `torch` | 0.10 x 0.06 x 0.23 | 13252 | 2 baked | metal, nrm, rough | a torch |
| `trashbag` | 0.53 x 0.57 x 0.46 | 4482 | 1 baked | nrm, rough | rubbish |
| `vase` | 0.20 x 0.40 x 0.20 | 10296 | 1 baked | nrm, rough | vases |
| `wardrobe` | 1.14 x 1.88 x 0.49 | 26406 | 1 baked | metal, nrm, rough | wardrobes and chests of drawers |

**Procedural, written by the game's generator.** `tools/mythos_assets.py` builds these from boxes when a game runs
`write_models` (the game then ships the OBJ): `GENERIC_MODELS` = `bed`, `toilet`, `shower`, `fridge`, `washer`, `sofa`,
`table`, `chair`, `bookshelf`, `wardrobe`, `sink`, `pine` (the fallback for names the engine does not ship as scans; a game's
generator should skip a name that `assets/models/` already has), and the figure the body code poses: `body_torso`,
`body_head` (origin at the neck pivot), `body_leg` (origin at the hip, hangs down), `body_arm` and `body_forearm` (origin at
the shoulder / elbow, point along +z), plus `flame` (a lighter's flame, material `glow_flame`). They use the common textures
`cloth`, `skin`, `paper`, `ink`, `wood`, `metal`, `enamel`, `pine` and `flame` (`gfx/materials.h`).

**Using them.** `world/body.h` poses the figure (`PoseBody`) and holds the light (`torch`, or `lighter` with its `flame`);
`platform/gl/body_instances.h` uploads those models (`UploadBodyModels`) and turns a pose into the frame's model
instances (`AddBodyInstances`); `CameraState::mirror` shows the figure in a wall mirror (`world/mirror.h` places it from
a prop: `PlaceMirror`, then `SetMirror`). `HandSway` and `BodyMotion` (`world/body.h`) give the hand's lag and sway and the
chest's turn frame by frame, from `HandSwayParams` (`LoadHandSwayParams`). The torch and lighter orientation
is baked into their OBJ (the lighter stands with its wide face towards +x and its lid hinged to one side): the `HeldParams`
defaults match.

## Tools

- **`tools/mythos_assets.py`**: a Python library for a game's own asset generator. `Mesh` (boxes and quads by material,
  written as OBJ), `Grid` and `write_level` (draw rooms on a character map, write a level file), `GENERIC_MODELS`
  (bed, toilet, shower, fridge, washer, sofa, table, chair, bookshelf, wardrobe, sink, pine; and the figure `body_*`, `flame`) and `write_models`
  (write a set of model builders as OBJ files).
- **The level editor** (`MythosEditor`, the `mythos::editor` library): a native editor in the manner of Unity, Unreal
  and Blender, for any game laid out as `assets/levels` + `assets/data/tiles.json`. A 3D viewport (orbit and fly
  camera, a cutaway that hides the ceiling, ghosted floors above and below, a move / rotate gizmo, tile painting,
  placing models; furniture is solid in the cell its centre is in but is placed freely inside it, by drag, nudge or
  the Inspector's Offset and Lift, so a piece can touch a wall) with dockable panels around it: Levels, Outliner, Inspector (every key of a game's JSON object),
  Tiles, Assets, Problems (furniture in walls, missing models, stairwells without a hole), Console and a **Game
  Preview** that runs the game's own screenshot mode on a copy of its assets holding the *unsaved* edits. It has undo
  and redo, duplicate, delete, new level and resize, and saves only what changed (a level is stamped `map_editor` so a
  generator script leaves it alone; the data files round-trip byte for byte). It is built with Dear ImGui (docking,
  fetched at `MYTHOS_IMGUI_TAG`) and is on by default when the engine is the top-level project
  (`MYTHOS_BUILD_EDITOR`); a game that wants it sets `MYTHOS_BUILD_EDITOR=ON` before fetching the engine.

  Started with no arguments (from a file manager) it opens the last project, or asks for the game's folder (the one holding `assets/`);
  models and textures come from the project's `assets/`, or from the `assets/` shipped beside the editor.
  Each tagged release attaches `MythosEditor` builds for macOS (universal), Linux and Windows, with the engine's assets.
  The builds are not signed or notarized: on macOS clear the download's quarantine flag once (`xattr -cr MythosEditor.app`) or
  allow it under System Settings > Privacy & Security.

  `MythosEditor --project <game repo> [--assets <merged asset stage>]` edits levels, furniture and decor. A game puts
  its own things in it (props, lamps, rooms and spawn points kept in its own JSON files, which levels belong to which
  scenario, how to run the game for the preview) by deriving from `mythos::editor::Adapter`
  (`engine/editor/adapter.h`) and calling `mythos::editor::RunEditor(argc, argv, config)` from its own `main()` (the
  file that includes `<SDL3/SDL_main.h>`), then linking `mythos::editor`:

  ```cpp
  class MyAdapter : public mythos::editor::Adapter {
    std::vector<std::string> DataFiles() const override { return {"props"}; }  // assets/data/props.json
    // Entities, Describe (how the viewport draws a thing), GetPose / SetPose, Label, Data (the Inspector's
    // JSON), Duplicate / Remove, Info / Contexts / Stack (levels and floors), Preview (the game's command line)...
  };
  ```

  The adapter is pure logic over JSON (`DataSet` is the files by name, and the editor loads, saves and undoes them),
  so it is unit-tested without a window; the engine's own tests do it with a small fake adapter. Every virtual has a
  default, so an adapter overrides only what its game has. `--shot out.ppm [--size W H] [--frames N] [--camera ...]
  [--top] [--select furniture:N] [--tool paint] [--preview]` renders the window headless, for checking the UI.
- **`mythos_pack <out.pak> <assets dir> <key text>`**: packs every file under the folder into one file. At run time,
  `mythos::vfs::mount` serves reads below a root from the pack, and anything outside falls through to the disk. This is
  obfuscation to stop casual editing, not DRM: the key is in the executable.

## Conventions

- One map cell is one world unit; world x is the column and z is the row; model fronts face `+z`.
  Yaw 0 is south (+z), 90 is east.
- Room doors are the glyphs `N` (closed, solid) and `n` (open); the legend must define both as doors.
- All player-facing text goes through `Tr("dotted.key")` with tables registered by the game.
- Every transition is seamless; the engine has no fail state of its own.

## Build and test

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure      # or ./build/mythos_engine_tests <name-substring>
# the tests need no SDL and no display: -DMYTHOS_BUILD_PLATFORM=OFF
```

Warnings are on (`-Wall -Wextra -Wpedantic`, `/W4`); keep the build warning-free. Code is formatted with `clang-format` (`.clang-format`, Google style) and checked with `clang-tidy` (`.clang-tidy`); CI fails on any warning, formatting difference or tidy finding. The test harness is
home-made: `TEST(name) { CHECK(...); CHECK_EQ(a, b); }` in `tests/test_main.cpp`. `.editorconfig` pins the
style (4 spaces, LF, final newline).

## Assets and credits

The engine ships only what games share: fonts, common sounds and a few textures in `assets/`. Anything new must
be CC0 or OFL and credited in [CREDITS.md](CREDITS.md). `third_party/stb/stb_truetype.h` and `stb_image.h` are
public domain (Sean Barrett).

## License

MIT, see [LICENSE](LICENSE). Bundled `stb_truetype` and `stb_image` are public domain; SDL3 (zlib), nlohmann/json (MIT) and GLM (MIT) are fetched at configure time; see [CREDITS.md](CREDITS.md).
