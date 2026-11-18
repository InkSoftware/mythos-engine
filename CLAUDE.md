# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

mythos-engine is a C++20 library (not an application) for first-person grid games on SDL3 + OpenGL 4.1 core. Games live in other repos and consume it via `FetchContent`/`add_subdirectory`. See `README.md` for the consumer-facing description.

## Commands

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/mythos_engine_tests <name-substring>     # run a subset of tests by name
cmake -S . -B build -DMYTHOS_BUILD_PLATFORM=OFF  # headless: no SDL, no display needed (tests don't need it)
```

- `mythos_pack` (the asset packer, `tools/pack/`) is built with everything else; `mythos_engine_tests` only with `MYTHOS_BUILD_TESTS`.
- CI (`.github/workflows/ci.yml`) builds and runs the tests on ubuntu-22.04, macos-26 and windows-2022, each with `MYTHOS_PORTABLE` ON and OFF; Dependabot keeps the actions current (weekly).
- Defaults to Release when top level. There is no linter; warnings (`-Wall -Wextra -Wpedantic`, `/W4`) must stay clean. `stb_truetype_impl.cpp` and `stb_image_impl.cpp` have warnings suppressed on purpose.
- `.editorconfig` pins the style (4 spaces, LF, final newline); there is no formatter.
- Options: `MYTHOS_BUILD_PLATFORM`, `MYTHOS_BUILD_TESTS`, `MYTHOS_FETCH_SDL`, `MYTHOS_PORTABLE`, `MYTHOS_LTO`, `MYTHOS_SDL_VERSION`.
- Tests are a single home-made harness in `tests/test_main.cpp`: `TEST(name) { CHECK(...); CHECK_EQ(a, b); }`. It links only `mythos_engine`, so tests can never touch SDL/GL code.
- `src/**/*.cpp` files are listed explicitly in `CMakeLists.txt`; add new sources there. Headers are included as `"engine/<folder>/<file>.h"` (the include root is `src/`; folders are listed below).

## Architecture

Two layers, with a hard dependency boundary:

- **`mythos::engine`** (`src/engine/{core,world,audio,data,gfx,ui}/`): pure logic and data. Must not include SDL or OpenGL and must know nothing about any particular game. This is what makes game rules testable headless. Everything is in `namespace mythos`.
- **`mythos::engine_platform`** (`src/engine/platform/*`): the only code that touches SDL3/GL (window, `SceneRenderer`, audio mixer, input, frame clock). GL entry points are loaded at runtime via SDL (`gl_api`), so no GL library is linked.
  - `gl_objects` holds the RAII wrappers (`GlBuffer`, `GlVertexArray`, `GlTexture`, `RenderTarget`, `GlProgram` with cached named uniforms); use them rather than raw `GLuint`s. `Window::close()` calls `gl::unloadFunctions()`, so wrappers may outlive the context safely.
  - `SceneRenderer` orchestrates the passes (shadows, sky, world into a multisampled target, composite with motion blur / lens / FXAA, overlay); `TextRenderer` draws the SDF text. World geometry is split into 8-cell chunks at upload (one VAO/VBO per texture, chunks as contiguous ranges) and frustum-culled per frame; neighbouring visible chunks merge into one draw. The overlay `Surface` is uploaded as-is (BGRA, flipped in the quad shader) and skipped when unchanged.
  - Files of the renderer: `scene_renderer.cpp` (passes), `scene_shadows.cpp` (lamp choice, cube maps, the cache), `shaders/vertex/*.vert` and `shaders/fragment/*.frag|glsl` (all GLSL, embedded at build time into a generated source by `cmake/EmbedShaders.cmake`; `scene_shaders.h` declares the strings), and `engine/core/mat4.h` (matrices, frustum and cube-face views, headless and tested).
  - Colour textures are uploaded as `SRGB8_ALPHA8` (the shader gets linear values from sampling, no `pow`); normal maps stay `RGBA8`.
  - Model instances and the mirror: `uploadModel(batches)` returns an id; a frame's `CameraState::instances` draws those models at the matrices the caller gives (`ModelInstance`: `views` = `kMain` and/or `kMirror`). They are drawn in the main pass and the mirror pass but never into the shadow cubes (a torch in the hand must not shadow its own light). `CameraState::mirror` (`MirrorState`: glass centre, horizontal normal, half size) turns on one planar mirror: `drawMirrorView` draws the world again into `mirrorTarget_` (3/4 size) with the view reflected about the glass plane (`view * R`; the lighting stays real, lamps and the eye are not moved) and `uClip` (world shader) dropping everything behind the plane plus 1.5 cm, which removes the wall and the mirror prop's own face; `drawMirrorGlass` then lays a quad over the glass that samples that target by screen position (`mirror_glass.vert/.frag`), tinted and slightly translucent. It runs only when the glass faces the eye and is in the frustum.
  - Basic models: `assets/models` + `assets/modeltex` ship the basic models of houses and offices (furniture, household things, picture frames, a torch), baked from Poly Haven by `tools/fetch_cc0_models.py` through `tools/mythos_models.py` (the conversion a game's own fetch tool reuses; `assets/data/model_credits.json` is what a game lists on its credits page). The picture frames show the `painting` texture: a game overrides it with its own art. A game's assets are laid over these, so a model of the same name replaces one; a game's generator must not write a procedural model of a name shipped here (the `GENERIC_MODELS` builders are the fallback).
  - Levels: besides `[furniture]` (solid, one cell), a `[decor]` section of `x y model yaw [offX offZ lift]` lines places non-solid pieces (a vase on a table, a frame on a wall); `tools/mythos_assets.py write_level(..., decor=)` writes it and `tools/map_editor.html` keeps it untouched.
  - Normal mapping: every texture gets a normal map made from its own brightness (`gfx/normalmap.h`, strength `kNormalStrength` in `scene_renderer.cpp`), bound on unit 6. The world shader builds the tangent frame from screen-space derivatives (the vertices carry no tangents) and bends the normal for lamp, moon and hand-light lighting only; shadow bias still uses the true normal.
  - PBR, metallic-roughness (`gfx/pbr.h`): a texture `X` may have companions `X_nrm` (tangent-space normal, glTF +y up), `X_rough`, `X_metal` and `X_ao` (greyscale, green channel read), registered like any texture and not drawn on their own. `makeSurfaceMap` packs the normal (authored, else brightness-made) and gloss (alpha = 255 - roughness; 0 = matt) into unit 6; `makeMaterialMap` packs R metal, G occlusion into unit 7 (one shared default 1x1 texture for the many textures with neither). The world shader: GGX / Schlick-Smith / Fresnel specular for the lamps, moon and hand light (F0 0.04, or the albedo for a metal, which has no diffuse), baked occlusion dimming the ambient term, and reflections of the era's ambient light (no probes; Karis' analytic environment BRDF, specular occlusion). A surface with no companion renders pixel-identically to before. The `materials` setting (`CameraState::materials`, uniform `uMaterials`): 0 off (no specular, metal or occlusion), 1 basic (no environment reflections), 2 full; saved as `materials` in settings.txt.
  - Shadows: the nearest lit lamps (1 to 4 by level) and the hand light each have a cube map of distances (`ShadowCube`, sharing one `DepthBuffer`). A cube face is redrawn only when its lamp or the scene (geometry revision, scroll, doors) changes (keys are kept per face); the hand light draws only the faces its cone can reach (`handFaces_`, mirrored by the shader's `uHandFaces`, which treats undrawn faces as unshadowed, so keep the cone cosines in `chooseLamps` and the shader in step): bump `sceneRevision_` when a new kind of upload changes what casts shadows, and add anything that moves to `sceneKey`. The settings' counts (`kShadowLevelCount`, `kRenderScaleLines`, `kAntiAliasSamples`) live in `settings.h`; the renderer reads them, so keep new levels there.
  - Settings: `settings.h` (values, pages, persistence), `settings_ui.h` (`drawSettingsPage`, `MenuTheme` holds every layout number), `platform/apply_settings.h` (applies them to window, renderer, mixer, camera), and built-in English and Greek strings in `engine_strings.cpp`, which `tr()` falls back to. A new setting is added here only; the game just picks up the row.
  - JSON loaders for shared formats: `loadAchievementDefs`, `loadPlayerParams`, `loadSoundBank`, `loadTranslation`, `loadCredits`, `loadFontFiles`.
  - The context is OpenGL 4.1 core with `#version 410 core` shaders: the ceiling on macOS. Don't use 4.2+ features (compute, SSBOs, `glBufferStorage`, DSA) without a fallback.
- Third-party code: `stb_truetype` (fonts) and `stb_image` (texture overrides: PNG, BMP, TGA, JPG; vendored in `third_party/stb`), plus nlohmann/json 3.12.0 and GLM 1.0.3, which CMake takes from an installed copy or fetches (`FetchContent` in `CMakeLists.txt`; bump the version there and in the `find_package` line). nlohmann/json is a public dependency of `mythos::engine` (it is in `json.h`); GLM is private.
- **`mythos::sdl_main`**: interface target for SDL_main.h; `MYTHOS_EXE_KIND` (set to the parent scope) picks WIN32 / MACOSX_BUNDLE / plain for game executables.

Folders of `src/engine/` (include as `"engine/<folder>/<file>.h"`); a folder may include only the ones to its left in this order, so there are no cycles:

- `core/`: `math`, `mat4` (matrices through GLM, frustum, cube-face views), `noise`, `json` (nlohmann `ordered_json` as `json::Value`, plus a strict `parse` that rejects duplicate keys) and `json_reader`, `fileio`, `vfs`.
- `world/`, `audio/`, `data/` (each depends on `core/` only): `world/` is the grid world and first-person rules (`level`, `tiles`, `movement`, `player`, `room_doors`, `interact`); `audio/` the sound arithmetic and the sound bank (`audio_math`, `sound_bank`); `data/` the shared data formats and localisation (`achievements`, `credits`, `i18n`, `translation`, `engine_strings`).
- `gfx/` (core, world): the CPU side of graphics (`surface`, `textures`, `materials`, `mesh`, `model`, `normalmap`, `pbr`, `font` (the 5x7 bitmap font), `text` (SDF text layout), `flicker`).
- `ui/` (data, gfx): `ui` (overlay boxes, text, hotspots), `menu`, `settings`, `settings_ui`.
- `platform/` (everything above, and SDL / GL): `window`, `input`, `audio`, `app`, `apply_settings`, and `platform/gl/` for the renderer (`gl_api`, `gl_objects`, `scene_renderer` with `scene_shaders` and `scene_shadows`, `text_renderer`).

The engine supplies mechanisms and a game supplies meaning: games own a `Level`, `Player`, `PlayerWalker`, `RoomDoors`, `Interactor`, and a `Progress`-derived state, and turn `Use` outcomes (`interact.h`) into text, sound and plot. Prop texts are translation keys resolved through `tr("dotted.key")` (`i18n`); the engine never holds story strings.

`tools/map_editor.html` is a browser level editor (Chrome or Edge) for any game laid out as `assets/levels` + `assets/data/tiles.json`. `tools/mythos_assets.py` is a shared Python library that game-side generator scripts import to build OBJ models (`Mesh`) and level files (`Grid`, `write_level`); its output formats must stay in sync with `model.cpp` (OBJ parser, `glow_<texture>` materials) and `level.cpp` (`[map]` + `[furniture]` sections). `materials.h` defines the common texture names every game gets via `addCommonMaterials()`.

## Conventions that are easy to break

- One map cell = one world unit; world x = column, z = row, y up. Model fronts face `+z`; yaw 0 faces south (+z), 90 faces east (+x). Python tooling and C++ share this.
- Room door glyphs are `N` (closed, solid) and `n` (open); a tile legend must define both as doors.
- The engine has no fail state; transitions are seamless.
- Source is UTF-8 (the built-in Greek strings, a few glyphs); MSVC builds with `/utf-8`, which is `PUBLIC` so games get it too.
- MSVC rules that GCC and clang let slide: no raw string literal containing quotes inside a `CHECK(...)` argument (hoist it into a local; it breaks stringizing), no `fopen` / `sscanf` (C4996: use streams), and no passing `float[N][M]` where `const float (*)[M]` is expected (pass a struct such as `Frustum`).
- The engine ships only the assets games share: fonts, common sounds, a few textures and the roughness / metal companion maps of its common materials (`assets/modeltex/`, made by `tools/mythos_pbr_maps.py`; a game that registers its stage's `modeltex/*.bmp` as textures gets them) in `assets/` (games merge their own over them; `MYTHOS_ENGINE_ASSET_DIR` names the folder). Anything added must be CC0/OFL and recorded in `CREDITS.md`.
