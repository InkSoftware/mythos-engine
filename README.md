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
| `mythos::engine` | the standard library, `third_party/stb`, nlohmann/json, GLM | levels, tiles, movement, the player, room doors, props and verbs, `Progress`, meshes, OBJ models, textures and common materials, SDF fonts and text layout, the overlay toolkit, menus and the settings pages, i18n and the built-in strings, JSON, achievements, credits, the sound bank, the virtual file system, matrices and the view frustum, audio and light arithmetic |
| `mythos::engine_platform` | `mythos::engine`, SDL3 | window (vsync, fullscreen), the GL 4.1 renderer (RAII GL wrappers, shadow cube maps, multisampled scene target, composite pass, SDF text), the audio mixer, keyboard and mouse input, user files, frame clock and frame pacer, menu pointer, `applySettings` |
| `mythos::sdl_main` | SDL3 | link into an executable so SDL is linked; include `<SDL3/SDL_main.h>` in the file defining `main` |
| `mythos_pack` | `mythos::engine` | a tool that packs a game's assets folder into one file for the virtual file system (see Tools) |
| `mythos_engine_tests` | `mythos::engine` | the unit tests (no SDL, no display) |

`mythos::engine` knows nothing about SDL, OpenGL or any game, so every rule built on it can be tested
headless.

## Layout

```
src/engine/   the engine, by kind, included as "engine/<folder>/<file>.h"
assets/       fonts, common sounds and textures that games share
tools/        mythos_assets.py, map_editor.html, pack/ (mythos_pack)
tests/        test_main.cpp
third_party/  stb_truetype, stb_image
```

The sources are in folders by kind: `core/` (math, matrices, JSON, files, packed assets), `world/` (levels, tiles,
movement, the player, doors, props), `audio/` (sound arithmetic, the sound bank), `data/` (achievements, credits,
translations), `gfx/` (surfaces, textures and their derived normal maps, meshes, models, fonts and text, light arithmetic), `ui/` (overlay, menus,
settings) and `platform/` (window, input, mixer; `platform/gl/` is the renderer). A folder depends only on the ones
before it in that list (`gfx/` also on `world/`), apart from `platform/`, which uses them all.

## Using it

```cmake
include(FetchContent)
FetchContent_Declare(mythos_engine GIT_REPOSITORY <url> GIT_TAG v1.0.0-alpha)
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

Targets Ubuntu 22.04+ (GCC 11+), macOS and Windows 10+ (MSVC 2022), with CMake 3.22+, hence C++20. CI builds and tests
all three, each with `MYTHOS_PORTABLE` on and off (`.github/workflows/ci.yml`). SDL3 is fetched and built from source when no
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
- **Data**: levels (`[map]` + `[furniture]`), a tile legend, props, sounds, translations.
  The engine reads the shared formats for you: `loadSoundBank`, `loadTranslation`, `loadAchievementDefs`,
  `loadCredits`, `loadPlayerParams`, `loadFontFiles`, `parseTileSet`.
- **Settings**: `GameSettings` with its pages (`updateSettingsPage`), `drawSettingsPage` (give it a `MenuTheme`),
  and `platform/apply_settings.h` (`loadSettings`, `saveSettings`, `applySettings`). Their English and Greek
  strings are built in (`engine_strings.cpp`); the game's own tables override any of them.
- **Assets**: `assets/` holds fonts, common sounds (steps, doors, torch, battery, tick, static, wind) and eight
  common textures. A game merges it under its own assets (the game's file of the same name wins);
  `MYTHOS_ENGINE_ASSET_DIR` names the folder for CMake.
- **Art**: `addCommonMaterials()` first, then its own textures (PNG, BMP, TGA or JPG files named after the texture
  override the built-in ones); models as OBJ (`tools/mythos_assets.py`
  builds them and the level files from Python).
- **A front end**: build the camera and lights each frame (`CameraState`, then `applySettings(settings, camera)`),
  poll input with `pollInput`, play cues with `playCue`, upload geometry to `SceneRenderer`.

## Tools

- **`tools/mythos_assets.py`**: a Python library for a game's own asset generator. `Mesh` (boxes and quads by material,
  written as OBJ), `Grid` and `write_level` (draw rooms on a character map, write a level file), `GENERIC_MODELS`
  (bed, toilet, shower, fridge, washer, sofa, table, chair, bookshelf, wardrobe, sink, pine) and `write_models`
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
- All player-facing text goes through `tr("dotted.key")` with tables registered by the game.
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
