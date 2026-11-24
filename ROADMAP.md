# Roadmap

Gaps against Unity and Unreal, ordered by value for a first-person grid game
library. This is a plan, not a promise: each item is meant to ship on its own.

## Ground rules for every item

- Respect the layering in `CLAUDE.md`. Logic and data go in
  `src/engine/{core,world,audio,data,gfx,ui,editor}` with no SDL or GL and
  with unit tests in `tests/test_main.cpp`. Only `platform/` touches GL/SDL.
- GLSL stays `#version 410 core` (the macOS ceiling). No compute, SSBOs,
  `glBufferStorage` or DSA without a fallback.
- Every new setting goes in `settings.h` / `engine_strings.cpp` (English and
  Greek) only, so games pick it up for free.
- New `.cpp` files are listed in `CMakeLists.txt`. Code is clang-format and
  clang-tidy clean, with `-Werror` in CI, MSVC-safe (see the MSVC rules).
- Assets added must be CC0/OFL and recorded in `CREDITS.md`.
- Every item updates the Markdown files (`CLAUDE.md`, `README.md`, this roadmap, `CREDITS.md`) and `template-game/`
  (`CLAUDE.md` has the rule): what a new game would want from the item is shown there.
- A feature that changes how the world looks must leave existing scenes
  pixel-identical when its setting is off (as the PBR work did).
- The engine supplies mechanisms, games supply meaning: no story strings, no
  game names, in any new module.

Size guide: S is up to about 2 days, M about a week, L two weeks or more.

Status: every item below has a first working version, with a **Progress** line saying what is built and what is not.
"done" in a heading means nothing the item set out to do is left; "first version" means it works, is tested, is shown in
`template-game/` and has gaps listed in its Progress line. The **Where** and **Why** lines are the original plan and may be
more than was built.

---

## Phase 1: Look and feedback (the cheapest visible wins)

### 1.1 Tonemapping and colour grading (S) - done
- **Why:** like Unity/Unreal post-process volumes. Bloom exists but there is
  no tonemap, exposure or grade.
- **Where:** `lens_and_fxaa.frag` (composite). New `CameraState::grade`
  (`exposure`, `contrast`, `saturation`, `vignette`, `grain`, a tonemap enum,
  optional 3D LUT). CPU side: `gfx/grade.h` (LUT build and parse from a
  strip PNG, headless and tested).
- **Settings:** none. It is the game's look per level, not a player option
  (changed from the first plan).
- **Tests:** LUT identity, clamping, parse errors.
- **Done when:** off is pixel-identical, and a game can set a per-level grade.
- **Risk:** 3D textures are fine on GL 4.1; keep the LUT 32^3.

### 1.2 Particles (M) - done
- **Why:** dust, sparks, smoke, steam, rain splashes. The largest visual gap.
- **Where:** `gfx/particles.h` is a CPU emitter and simulator (spawn rate,
  lifetime, velocity cone, gravity, drag, size/colour over life, cell
  collision against `Level`), all deterministic from a seed and testable.
  `platform/gl/` draws billboards in the blended pass (built on the CPU, not
  instanced). Soft edges from the depth buffer and lit particles are not done:
  the sprites are unlit and a game tints them.
- **API:** `CameraState::particles` (a list of emitters or a flat particle
  buffer); a game owns emitters, the engine simulates and draws.
- **Tests:** determinism, lifetime and spawn counts, collision with walls.
- **Risk:** lighting. Start unlit or ambient-lit, with an optional glow.

### 1.3 Decals (S to M) - first version
- **Progress:** cell-bounded decals, clipped thin-wall and window-frame decals,
  a separate blended render pass and `[decals]` persistence are implemented.
  Cell-wall decals require a visible neighboring thick wall; thin-cell floor
  and ceiling decals are clipped to available quarters. Window-glass decals
  are clipped to the glass; prop decals remain. Glass decals render before
  blended glass and are not restricted to one viewing side. The editor viewport
  displays decals in a separate blended pass; visual verification remains.
  A model-local quad builder is available for prop decals, but attachment,
  persistence, clipping and automatic rendering are not yet implemented.
- **Why:** bullet holes, stains, blood, footprints, drips.
- **Where:** `world/decal.h` places a quad on a wall, floor or prop surface
  (cell, face, offset, size, rotation, texture) and bakes them into the scene
  geometry as an extra batch with a polygon offset. No projective decals yet.
- **Tests:** placement on each face, culling, a cap with oldest-first removal.
- **Done when:** a decal list persists in a level file's `[decals]` section.

### 1.4 Camera shake and tweens (S) - done
- **Where:** `core/tween.h` (easing functions, a `Tween<T>`, a sequencer) and
  `world/camera_fx.h` (trauma-based shake, FOV kick). Headless and tested.
- **Done when:** the camera consumes them with no GL code change. Done through
  `ApplyCameraFx` and one new `CameraState::fov_scale` read. Roll is left out
  for now: it needs the view matrix and the sky basis to change.

### 1.5 Debug overlay and console (M) - done
- **Why:** Unity's Stats and Unreal's `stat` and console variables.
- **Where:** frame time, draw calls, light and shadow counts, chunk counts from
  `SceneRenderer` (a `RenderStats` struct). A `core/cvar.h` registry
  (typed variables, get/set by name, tested). An overlay panel drawn with the
  existing `ui` boxes, toggled by a key; a free-fly camera.
- **Done when:** every setting is reachable as a cvar. Done. Chunk counts and
  GPU timings are not included (no GPU timer queries are loaded); the frame
  time is the game's own clock.

---

## Phase 2: Game logic (makes the editor useful for designers)

### 2.1 Triggers and event graph (M to L) - done (no visual graph)
- **Progress:** `world/trigger.h` provides a headless, single-subject trigger
  runtime with box/cell volumes, ordered enter/exit/stay/use events and once
  flags. A headless event-rule runtime dispatches generic actions for trigger,
  flag and timer events. Level files and editor documents persist `[triggers]`
  and `[rules]`. `world/event_graph.h` connects level triggers and rules for a
  single sampled subject; games must supply the update, flags, timers and action
  callbacks. The editor can add, edit, delete and display trigger volumes;
  renaming a trigger rewrites the rules it drives and deleting it deletes them
  (`RenameTrigger`, `RemoveTrigger`). A Rules section in the Inspector adds,
  edits (as a draft, applied when valid) and deletes rules. A visual graph
  remains. Rule strings use a hex-encoded line
  format (documented in `core/events.h`).
- **Why:** UnityEvents and Blueprints. Today a game hand-writes every rule.
- **Where:** `world/trigger.h` (box and cell trigger volumes: enter, exit,
  stay, use, with a once flag) and `data/events.h` (a data-driven rule: on
  `<trigger|flag|timer>` if `<conditions>` do `<actions>`; actions are an
  open enum plus a game callback table). A level file gets `[triggers]` and
  `[rules]` sections; the editor draws and edits triggers (viewport gizmo,
  Inspector rows) through the `Adapter`.
- **Tests:** parse round-trip, edge-triggered enter/exit, once flags, ordering.
- **Risk:** keep the action set generic (set flag, play sound, open door,
  show text key, spawn, call game id). Anything else is the game's.

### 2.2 Save and load (M) - first version
- **Progress:** `data/save.h` provides the versioned JSON file (named
  sections), per-version migrations, numbered slots and atomic writes;
  `world/save_state.h` serialises `Progress` and `Player`. Door and `Level`
  diffs: `LevelDiffToJson` / `ApplyLevelDiff` save the changed map cells (doors included); the template also saves the guide
  as its own section. Props taken from a level are a game's own section.
- **Where:** `data/save.h` is a versioned JSON snapshot (`Progress`, player,
  doors, flags, the `Level` diffs, game-supplied blob), a slot API and atomic
  writes (temp file then rename, via `fileio`). A migration hook per version.
- **Tests:** round trip, a corrupt file, forward version rejection.

### 2.3 Input actions and rebinding (M) - first version
- **Progress:** `ui/input_map.h` has named actions with contexts, up to two
  bindings each (keys, pad buttons, axis halves, mouse buttons, written as
  text), a conflict check per context with refuse-or-steal rebinding, reset,
  JSON persistence of the differences only and dead-zone maths,
  `ActionTracker` (per-frame state) and `DefaultGameplayMap` (the usual keys);
  `ProbeBinding`, `ApplyActions` and `InputPoll::pressed` feed a game from it (the
  template's Esc opens the settings menu, F3 or its Controls row the controls page, which saves the `bindings` line of `settings.txt`). The rebind page is
  `ui/controls_ui.h` (`UpdateControlsPage` tested, `DrawControlsPage`). The settings menu has a Controls row
  (`GameSettings::controls_page`, `SettingsPage::kControls`, `LeaveControlsPage`). The bindings are saved in settings.txt (`GameSettings::bindings`).
- **Why:** Unity Input System, Unreal Enhanced Input.
- **Where:** `ui/input_map.h` has named actions bound to keys, buttons and
  axes, with contexts (gameplay, menu), a conflict check, persistence in
  settings, and a rebind page in `settings_ui`. `PollInput` stays and
  feeds from it.
- **Tests:** binding, conflicts, serialization, dead zones.

---

## Phase 3: Life (the largest, so it comes after the cheap work)

### 3.1 Skeletal animation (L) - first version
- **Progress:** `gfx/skeleton.h` has the bone hierarchy (at most 64, parents
  first), keyframed clips, a sampler (loop or clamp), pose blending, an
  `Animator` crossfade and `SkinMatrices` (headless, tested). GPU skinning: a `Batch` with `skin`
  (`SkinWeights`, four joints per vertex) uploads as one uncullable chunk;
  `ModelInstance::skin` carries the bone matrices (at most 64, a `uBones`
  uniform array), used by the main, mirror and shadow passes (and in the
  shadow cache key). Static models draw exactly as before. Smoke-tested on
  macOS (shaders link, a skinned instance renders and casts a shadow), not
  yet compared against a real character. `gfx/gltf.h` loads a skinned
  glb (skeleton, skinned batches, every clip; CC0 stand-in
  `assets/characters/prototype_pete.glb`, KayKit, 30 clips). `gfx/anim_state.h` is the animation state machine
  (states, parameters, conditional and when-finished transitions with crossfades). `tools/mythos_models.py` keeps a rigged model with `Spec(skinned=True)` (one `.glb` in
  `assets/characters/`, tested by `tools/test_mythos_models.py`). Not done: external-buffer `.gltf` in the loader, sparse
  accessors, morph targets and textures on skinned models (the loader reads material colours).
- **Why:** models are static OBJ, so no animated NPCs, creatures or doors.
- **Where:** `gfx/skeleton.h` (bones, skin weights, clips, sampler, blend,
  crossfade, all CPU and tested), a glTF 2.0 loader for skins and clips
  (`gfx/gltf.h`; the pipeline can keep baking static scans to OBJ). The
  renderer skins in the vertex shader with a uniform array (cap bones at 64;
  no SSBOs), with the shadow pass using the same path. `ModelInstance` gets a
  pose handle. A small state machine (`gfx/anim_state.h`) is the Animator.
- **Tools:** `mythos_models.py` learns to keep skinned glTF.
- **Done when:** a CC0 animated character walks, idles and casts a shadow.
- **Risk:** the biggest item; split into loader, sampler, GPU skinning.

### 3.2 AI and pathfinding (M) - first version
- **Progress:** `world/nav.h` provides deterministic cardinal A* and a
  reachability flood, with optional conservative body-clearance checks and
  door avoidance. It treats thin-wall cells as blocked; sub-cell routing,
  smoothing remains. `world/ai.h` adds a reactive behaviour tree (leaf and
  condition callbacks, sequence, selector, inverter, succeeder, cooldown), a
  sight cone with line of sight and hearing that walls muffle. Not done: noise
  event queues, memory of last-seen positions, path smoothing.
- **Where:** `world/nav.h` is A\* on the grid with door and thin-wall awareness
  (`Level::ThinArms`, `BoxBlocked`), path smoothing and a reachability flood.
  `world/ai.h` is a minimal behaviour-tree runtime (sequence, selector,
  decorators, leaf callbacks) plus perception (sight cone with
  `LineOfSight`, hearing from noise events). Headless, tested.
- **Tests:** shortest paths, closed vs open doors, unreachable goals,
  perception ranges.
- **Risk:** keep leaves game-supplied; the engine owns traversal only.

---

## Phase 4: Atmosphere

### 4.1 Audio: occlusion, reverb zones, mixer groups (M) - first version
- **Progress:** `audio/spatial.h` has the occlusion maths, a low-pass cutoff and coefficient, the reverb send for a room size and
  `MixerGroups` (volume and ducking); `world/sound_path.h` counts walls and closed doors between a source and the
  listener; `AudioEngine::SetSourceMuffle` low-passes a positional source (the template's hum in the back room is
  muffled by its walls and closed door); `audio/reverb.h` is the reverb and `AudioEngine::SetReverb` applies it, the
  template sets it from the size of the room the player is in (`RoomDoors::RegionSize`). The mixer uses `MixerGroups` (`SetGroupVolume`, `AddGroupDuck`, a group per sound). Layered music (`audio/music.h`: `LayerFader`, `AudioEngine::StartMusic` / `SetMusicLayer` /
  `CrossfadeMusic` / `StopMusic`). Not done: the reverb is one room-wide send on the whole mix (no per-sound send), and
  no tempo or beat-synchronised transitions.
- **Where:** `audio/audio_math.h` gets cell-based occlusion (walls between
  source and listener, doors open or closed, a low-pass amount) and reverb
  zones from room size (`RoomDoors` regions). Mixer groups with volume and
  snapshots (duck music on dialogue) and a layered music crossfade. The
  mixer applies the maths; tests cover the maths.

### 4.2 SSAO (M) - first version
- **Progress:** done as a post pass (`ssao.frag` at half size, a blur, multiplied in the composite, less on bright
  pixels), behind the Ambient occlusion setting (off by default; `CameraState::ssao`); the scene target's depth is now a
  texture, copied out of the multisampled target only while it is on. The template shows it with `--ssao`. Not done:
  applying it to the ambient term inside the world shader (it would need the depth before the lit pass) and the mirror view.
- **Where:** a half-resolution pass beside bloom, multiplied into the ambient
  term only. Needs a depth/normal copy of the multisampled target (resolve).
  Setting: off / on, off keeps pixel parity.

### 4.3 Reflection probes (M) - first version
- **Progress:** `SceneRenderer::AddReflectionProbe` and `CameraState::reflections`: a 128 x 128 cube capture of the world at
  each probe (redrawn only when the scene changes), mips as the roughness blur, read in the world shader's
  environment reflection; off by default and unchanged then. The template shows it with `--reflections` (a toolbox
  and a bucket stand in the first room). Not done: parallax correction, a probe per chunk (only the one nearest the
  viewer is used), the sky and moving models in the capture, HDR capture (the cube holds tone-mapped colours) and
  a proper roughness filter (the mip chain is a box filter).
- **Where:** a probe is a cube capture of the scene at a position (reusing the
  shadow cube plumbing), filtered to a few roughness levels, assigned per
  chunk. The world shader replaces the analytic ambient term when one is
  bound. Captured on load and when the scene revision changes, not per frame.

### 4.4 Sun and day/night (S to M) - first version
- **Progress:** `gfx/sky.h` (`SkyAt(hour)`, `NightLook`) and `CameraState::time_of_day` drive the sun or moon's direction and
  light, ambient, the sky gradient, stars, clouds and haze of an outdoor scene; with it unset the night is as it was.
  The template shows it (`--outdoor --hour H`). A sun shadow (`CameraState::sun_shadow`, two orthographic cascades around the viewer, 26 m reach,
  `gfx/sun_shadow.h`) is done. Not done: shadows beyond 26 m (a third cascade or a per-level fit) and a sun-aware sky
  for the mirror and glass panes.
- **Where:** `CameraState::sun` (direction, colour, intensity), a time-of-day
  curve in `gfx/sky.h` (headless), one cascade or a single shadow map for
  outdoor scenes. The sky shader blends dawn to dusk.

---

## Phase 5: Scale and tools

### 5.1 Baked lighting (L) - first version (probes)
- **Progress:** light probes first: `gfx/probes.h` (`BakeProbes`, `ProbeGrid`, a text file; headless and tested) and
  `SceneRenderer::UploadProbes` (a float texture over the floor plan, added in `world_lighting.frag`; off and
  pixel-identical without an upload). The template bakes its lamps with `--baked`. Not done: lightmaps, probes in 3D
  with direction and baked light in the mirror view. `tools/bake/mythos_bake_probes` bakes a level and a lamps file into
  a `.probes` file (the template loads its checked-in `hall.probes`, and a ctest re-bakes it).
- Lightmaps or light probes for static lamps, lifting the 32-light limit for
  static geometry. A bake tool in `tools/` (CPU, cell-based), light probes
  first (cheaper, and they light dynamic props too).

### 5.2 Instancing and LOD (M) - first version
- **Progress:** `gfx/lod.h` plans each pass's model instances: a distance cull (`ModelInstance::max_distance`), a far model
  swapped in beyond a distance (`SceneRenderer::SetModelLod`) and a sort by model. Shadow casters are never culled or
  swapped. GPU instancing: runs of static instances of one model draw as one instanced draw per batch (200 chairs:
  13 draw calls instead of 212). Not done: instancing skinned models and LOD for the baked world geometry.
- Instanced draw for repeated props (same model, many matrices) and a
  distance LOD or cull per model. The 8-cell chunking already helps; this
  removes per-prop draw calls.

### 5.3 Asset hot-reload (S to M) - first version
- **Progress:** `core/file_watch.h` (`FileWatcher`), `SceneRenderer::ReloadShaders` (loose GLSL, a failed compile keeps the
  old program) and `ReloadTextures` (in place); the template watches its level, its textures and the engine's shaders
  and reloads each (`Game::ReloadLevel`). Models (`ModelLibrary::LoadFile`, `SceneRenderer::ReplaceModel`) and sounds
  (`AudioEngine::LoadDir` again) reload too. Not done: skinned `.glb` characters, and an editor hook.
- Watch textures, shaders and levels and reload without a restart. Dev builds
  only; the shader embed step needs a loose-file mode.

### 5.4 Editor prefabs (M) - first version
- **Progress:** `editor/prefab.h` (a `.prefab` file in the level's own line format, capture, quarter turns and placement with
  checks, tested) and a Prefabs section in the Inspector (save an area, place at a cell with a turn). The viewport's Prefab tool (P) places the chosen prefab at the clicked cell with a
  footprint ghost. Not done: picking the area to capture with the mouse, prefabs that keep a link to their file (an
  update that reaches the placed copies), and prefabs holding triggers or decals.
- Reusable groups of furniture and decor, saved as a file, placed and
  overridden in the editor. Mirrors the level format so games need no code.

### 5.5 Cutscene sequencer (L) - first version
- **Progress:** `world/cutscene.h` (a text file of camera keys, cues, value tracks and animation keys, a `CutscenePlayer` with
  `Skip`, tested); the template plays `assets/cutscenes/intro.cutscene` (camera glide, a fade, text, the guide's wave).
  The editor has a Cutscene panel (tables of keys, a key from the viewport camera, problems, save). Not done: a
  graphical timeline (scrubbing, dragging keys, a camera preview), sound and tween-callback tracks beyond cues and
  values, and a cutscene runner that other systems (the cvar console) can drive.
- Timeline of camera, tween, sound, event and animation tracks, built on 1.4,
  2.1 and 3.1. Editor timeline panel.

---

## Suggested order

The order the items were built in. All have a first version.

| Order | Item | Size | Depends on | Status |
|---|---|---|---|---|
| 1 | 1.1 Tonemap and grade | S | none | done |
| 2 | 1.4 Camera shake and tweens | S | none | done |
| 3 | 1.2 Particles | M | none | done |
| 4 | 1.3 Decals | S to M | none | first version |
| 5 | 2.1 Triggers and events | M to L | none | done (no visual graph) |
| 6 | 1.5 Debug overlay and console | M | none | done |
| 7 | 2.2 Save and load | M | 2.1 | first version |
| 8 | 2.3 Input actions | M | none | first version |
| 9 | 3.2 AI and pathfinding | M | 2.1 | first version |
| 10 | 3.1 Skeletal animation | L | none | first version |
| 11 | 4.4 Sun, 4.1 audio, 4.2 SSAO, 4.3 probes | S to M each | none | first versions |
| 12 | 5.x tools and scale | mixed | 5.5 needs 1.4, 2.1, 3.1 | first versions |

## What is left

Gaps named in the Progress lines, by area:

- **Rendering:** sun shadows beyond 26 m; the mirror and glass panes with the sun-aware sky, ambient occlusion, baked light
  and reflection probes; HDR and parallax-corrected reflection probes, a probe per chunk, a real roughness filter;
  lightmaps and probes in 3D with direction; instancing skinned models and LOD for baked world geometry; SSAO applied to
  the ambient term inside the world shader.
- **Animation and AI:** glTF with external buffers, sparse accessors, morph targets and textures on skinned models;
  noise events, memory of last-seen positions and path smoothing for the AI.
- **Audio:** a per-sound reverb send and beat-synchronised music transitions.
- **Editor:** a graphical cutscene timeline (the panel has tables), picking the prefab area to capture with the mouse, linked prefabs
  and prefabs with triggers or decals, a visual event graph, an editor hook for hot reload.
- **Tools and data:** furniture changes in a save.

## Out of scope for now

Networking and multiplayer, VR, full rigid-body physics (a kinematic prop
solver may come later), a visual scripting editor beyond the event rules in
2.1, and console ports.
