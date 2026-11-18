# mythos-engine

A small C++20 engine for first-person grid games on SDL3 and OpenGL 4.1 core, with no game engine
underneath. It carries games of one kind: a player walking a grid of rooms, doors, things to read,
listen to and pick up, menus, and a bit of procedural art.

- **World**: grid levels and tile legends, movement and pathfinding, line of sight, the player, room doors,
  props with verbs (`Interactor`), a notebook, clues and flags (`Progress`).
- **Rendering** (OpenGL 4.1 core): per-pixel lighting with lamps and a hand light (torch or lighter), shadows from
  distance cube maps (off to "ultra max"), MSAA or FXAA, motion blur, a lens effect, render scale from 300 to 2160
  lines, a night sky, culled world geometry, SDF text.
- **Everything around a game**: a settings system (pages, drawing, saving, applying), localisation with built-in
  English and Greek strings, JSON loaders for sounds, translations, achievements and credits, positional audio, packed
  assets, and procedural textures.

| Target | Needs | Holds |
| --- | --- | --- |
| `mythos::engine` | the standard library, `third_party/stb`, nlohmann/json, GLM | levels, tiles, movement, the player, the body and the light in its hand, room doors, props and verbs, `Progress`, meshes, OBJ models, textures and common materials, SDF fonts and text layout, the overlay toolkit, menus and the settings pages, i18n and the built-in strings, JSON, achievements, credits, the sound bank, the virtual file system, matrices and the view frustum, audio and light arithmetic |
| `mythos::engine_platform` | `mythos::engine`, SDL3 | window (vsync, fullscreen), the GL 4.1 renderer (RAII GL wrappers, shadow cube maps, multisampled scene target, composite pass, SDF text), the audio mixer, keyboard and mouse input, user files, frame clock and frame pacer, menu pointer, `ApplySettings` |
| `mythos::sdl_main` | SDL3 | link into an executable so SDL is linked; include `<SDL3/SDL_main.h>` in the file defining `main` |
| `mythos_pack` | `mythos::engine` | a tool that packs a game's assets folder into one file for the virtual file system (see Tools) |
| `mythos_engine_tests` | `mythos::engine` | the unit tests (no SDL, no display) |

`mythos::engine` knows nothing about SDL, OpenGL or any game, so every rule built on it can be tested
headless.

## Layout

```
src/engine/   the engine, by kind, included as "engine/<folder>/<file>.h"
assets/       fonts, common sounds and textures, and the basic house and office models, that games share
tools/        mythos_assets.py, mythos_models.py, fetch_cc0_models.py, mythos_pbr_maps.py, map_editor.html, pack/ (mythos_pack)
tests/        test_main.cpp
third_party/  stb_truetype, stb_image
```

The sources are in folders by kind: `core/` (math, matrices, JSON, files, packed assets), `world/` (levels, tiles,
movement, the player, doors, props, the body), `audio/` (sound arithmetic, the sound bank), `data/` (achievements, credits,
translations), `gfx/` (surfaces, textures and their derived normal maps (or authored `_nrm` / `_rough` companions, light PBR), meshes, models, fonts and text, light arithmetic), `ui/` (overlay, menus,
settings) and `platform/` (window, input, mixer; `platform/gl/` is the renderer). A folder depends only on the ones
before it in that list (`gfx/` also on `world/`), apart from `platform/`, which uses them all.

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

Options: `MYTHOS_BUILD_PLATFORM` (SDL layer, default ON), `MYTHOS_BUILD_TESTS` (default ON when top level),
`MYTHOS_FETCH_SDL` (download SDL when none is installed, default ON), `MYTHOS_SDL_VERSION` (the SDL release fetched,
3.2.28), `MYTHOS_PORTABLE` (link SDL statically, always fetched, so the app runs without SDL installed),
`MYTHOS_LTO` (link-time optimization for Release, on by default). MSVC builds use `/utf-8`, which games inherit.

Targets the releases each vendor still supports: Ubuntu 22.04+ (GCC 11+), macOS 15+ and Windows 11 (MSVC 2022), with
CMake 3.22+, hence C++20. On macOS `CMAKE_OSX_DEPLOYMENT_TARGET` defaults to 15.0 when the engine is the top-level project
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
- **Data**: levels (`[map]` + `[furniture]`, and `[decor]` for pieces that block nothing), a tile legend, props, sounds, translations.
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

## Models and textures

**Shipped in `assets/` (Poly Haven, CC0).** The basic models of houses and offices: furniture, household things,
picture frames, a torch and a lighter. They are OBJ files in `assets/models/`; each material is a texture named
`m_<model>_<n>` in `assets/modeltex/` (a game registers every BMP in that folder as a texture), with the optional
`_nrm`, `_rough`, `_metal` and `_ao` companions the Materials setting reads (`gfx/pbr.h`). Metric (a world unit is a metre),
feet on the floor and centred on the origin, front facing +z. They are baked by `tools/fetch_cc0_models.py` (the list) through
`tools/mythos_models.py` (the conversion; a game's own fetch tool reuses it, with its own list and root). A game's
assets are laid over these, so a model or texture of the same name replaces the engine's. Sources, authors and licences are
in `CREDITS.md`; `assets/data/model_credits.json` is the same list for a game's credits page. Colour maps are 256 or 512 px.
The picture frames show the `painting` texture (a "NO ART YET" placeholder: a game supplies its own). Their glass
and any other see-through material are left out, since the renderer does not blend.

| Model | Size w x h x d (m) | Triangles | Textures | PBR maps | What it is |
| --- | --- | --- | --- | --- | --- |
| `barrel` | 0.74 x 0.87 x 0.76 | 10820 | 1 baked | metal, nrm, rough | wine barrels |
| `bed` | 0.90 x 1.20 x 2.00 | 49990 | 1 baked | metal, nrm, rough | beds |
| `bookshelf` | 1.10 x 1.65 x 0.47 | 10106 | 1 baked | metal, nrm, rough | bookshelves |
| `boombox` | 0.58 x 0.38 x 0.15 | 10168 | 2 baked | metal, nrm, rough | a boombox |
| `bowl` | 0.31 x 0.09 x 0.31 | 14122 | 1 baked | nrm, rough | wooden bowls |
| `broom` | 0.49 x 1.45 x 0.11 | 14258 | 2 baked | metal, nrm, rough | brooms |
| `bucket` | 0.37 x 0.55 x 0.34 | 5116 | 1 baked | metal, nrm, rough | buckets |
| `cardbox` | 0.39 x 0.34 x 0.52 | 16952 | 1 baked | nrm, rough | cardboard boxes |
| `cassette` | 0.13 x 0.24 x 0.05 | 4830 | 3 baked | metal, nrm, rough | a cassette player |
| `chair` | 0.43 x 0.97 x 0.58 | 22013 | 1 baked | metal, nrm, rough | chairs |
| `cuttingboard` | 0.45 x 0.04 x 0.25 | 3520 | 1 baked | nrm, rough | kitchen counters |
| `dustpan` | 0.33 x 0.10 x 0.37 | 4332 | 1 baked | metal, nrm, rough | a dustpan |
| `frame_fancy` | 0.60 x 0.46 x 0.02 | 938 | 2 baked | nrm, rough | pictures on the walls |
| `frame_stand` | 0.20 x 0.25 x 0.10 | 1632 | 1 baked + `painting` | metal, nrm, rough | photographs on the tables |
| `frame_tall` | 0.59 x 0.84 x 0.02 | 2584 | 1 baked + `painting` | metal, nrm, rough | pictures on the walls |
| `frame_wide` | 0.75 x 0.50 x 0.03 | 3676 | 1 baked + `painting` | metal, nrm, rough | pictures on the walls |
| `gamepad` | 0.18 x 0.01 x 0.20 | 9546 | 1 baked | metal, nrm, rough | a game controller |
| `jug` | 0.28 x 0.21 x 0.17 | 5770 | 1 baked | metal, nrm, rough | jugs |
| `kettle` | 0.32 x 0.30 x 0.25 | 14838 | 2 baked | metal, nrm, rough | kettles |
| `laptop` | 0.39 x 0.33 x 0.29 | 14450 | 2 baked | metal, nrm, rough | a laptop |
| `lighter` | 0.01 x 0.06 x 0.05 | 3366 | 2 baked | metal, nrm, rough | a lighter |
| `nightstand` | 0.50 x 0.62 x 0.51 | 470 | 1 baked | nrm, rough | bedside tables |
| `oven` | 0.50 x 0.86 x 0.65 | 13914 | 2 baked | metal, nrm, rough | ovens |
| `pot` | 0.26 x 0.18 x 0.18 | 11416 | 1 baked | metal, nrm, rough | cooking pots |
| `sofa` | 0.85 x 1.07 x 0.77 | 5626 | 1 baked | nrm, rough | armchairs and sofas |
| `table` | 0.83 x 0.78 x 0.83 | 4254 | 1 baked | metal, nrm, rough | tables |
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
- **`tools/map_editor.html`**: a level editor that runs in the browser (Chrome or Edge). Open it, pick a game's
  repository root (the one with `assets/levels` and `assets/data/tiles.json`), edit a level and save.
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

Warnings are on (`-Wall -Wextra -Wpedantic`, `/W4`); keep the build warning-free. The test harness is
home-made: `TEST(name) { CHECK(...); CHECK_EQ(a, b); }` in `tests/test_main.cpp`. `.editorconfig` pins the
style (4 spaces, LF, final newline).

## Assets and credits

The engine ships only what games share: fonts, common sounds and a few textures in `assets/`. Anything new must
be CC0 or OFL and credited in [CREDITS.md](CREDITS.md). `third_party/stb/stb_truetype.h` and `stb_image.h` are
public domain (Sean Barrett).

## License

MIT, see [LICENSE](LICENSE). Bundled `stb_truetype` and `stb_image` are public domain; SDL3 (zlib), nlohmann/json (MIT) and GLM (MIT) are fetched at configure time; see [CREDITS.md](CREDITS.md).
