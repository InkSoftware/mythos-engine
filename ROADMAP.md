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
- A feature that changes how the world looks must leave existing scenes
  pixel-identical when its setting is off (as the PBR work did).
- The engine supplies mechanisms, games supply meaning: no story strings, no
  game names, in any new module.

Size guide: S is up to about 2 days, M about a week, L two weeks or more.

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

### 1.3 Decals (S to M)
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

### 2.1 Triggers and event graph (M to L)
- **Progress:** `world/trigger.h` provides a headless, single-subject trigger
  runtime with box/cell volumes, ordered enter/exit/stay/use events and once
  flags. A headless event-rule runtime dispatches generic actions for trigger,
  flag and timer events. Level files and editor documents persist `[triggers]`
  and `[rules]`. `world/event_graph.h` connects level triggers and rules for a
  single sampled subject; games must supply the update, flags, timers and action
  callbacks. Editor controls remain. Rule strings use a hex-encoded line format
  (documented in `core/events.h`), not a visual graph.
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

### 2.2 Save and load (M)
- **Where:** `data/save.h` is a versioned JSON snapshot (`Progress`, player,
  doors, flags, the `Level` diffs, game-supplied blob), a slot API and atomic
  writes (temp file then rename, via `fileio`). A migration hook per version.
- **Tests:** round trip, a corrupt file, forward version rejection.

### 2.3 Input actions and rebinding (M)
- **Why:** Unity Input System, Unreal Enhanced Input.
- **Where:** `ui/input_map.h` has named actions bound to keys, buttons and
  axes, with contexts (gameplay, menu), a conflict check, persistence in
  settings, and a rebind page in `settings_ui`. `PollInput` stays and
  feeds from it.
- **Tests:** binding, conflicts, serialization, dead zones.

---

## Phase 3: Life (the largest, so it comes after the cheap work)

### 3.1 Skeletal animation (L)
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

### 3.2 AI and pathfinding (M)
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

### 4.1 Audio: occlusion, reverb zones, mixer groups (M)
- **Where:** `audio/audio_math.h` gets cell-based occlusion (walls between
  source and listener, doors open or closed, a low-pass amount) and reverb
  zones from room size (`RoomDoors` regions). Mixer groups with volume and
  snapshots (duck music on dialogue) and a layered music crossfade. The
  mixer applies the maths; tests cover the maths.

### 4.2 SSAO (M)
- **Where:** a half-resolution pass beside bloom, multiplied into the ambient
  term only. Needs a depth/normal copy of the multisampled target (resolve).
  Setting: off / on, off keeps pixel parity.

### 4.3 Reflection probes (M)
- **Where:** a probe is a cube capture of the scene at a position (reusing the
  shadow cube plumbing), filtered to a few roughness levels, assigned per
  chunk. The world shader replaces the analytic ambient term when one is
  bound. Captured on load and when the scene revision changes, not per frame.

### 4.4 Sun and day/night (S to M)
- **Where:** `CameraState::sun` (direction, colour, intensity), a time-of-day
  curve in `gfx/sky.h` (headless), one cascade or a single shadow map for
  outdoor scenes. The sky shader blends dawn to dusk.

---

## Phase 5: Scale and tools

### 5.1 Baked lighting (L)
- Lightmaps or light probes for static lamps, lifting the 32-light limit for
  static geometry. A bake tool in `tools/` (CPU, cell-based), light probes
  first (cheaper, and they light dynamic props too).

### 5.2 Instancing and LOD (M)
- Instanced draw for repeated props (same model, many matrices) and a
  distance LOD or cull per model. The 8-cell chunking already helps; this
  removes per-prop draw calls.

### 5.3 Asset hot-reload (S to M)
- Watch textures, shaders and levels and reload without a restart. Dev builds
  only; the shader embed step needs a loose-file mode.

### 5.4 Editor prefabs (M)
- Reusable groups of furniture and decor, saved as a file, placed and
  overridden in the editor. Mirrors the level format so games need no code.

### 5.5 Cutscene sequencer (L)
- Timeline of camera, tween, sound, event and animation tracks, built on 1.4,
  2.1 and 3.1. Editor timeline panel.

---

## Suggested order

| Order | Item | Size | Depends on |
|---|---|---|---|
| 1 | 1.1 Tonemap and grade | S | none |
| 2 | 1.4 Camera shake and tweens | S | none |
| 3 | 1.2 Particles | M | none |
| 4 | 1.3 Decals | S to M | none |
| 5 | 2.1 Triggers and events | M to L | none |
| 6 | 1.5 Debug overlay and console | M | none |
| 7 | 2.2 Save and load | M | 2.1 |
| 8 | 2.3 Input actions | M | none |
| 9 | 3.2 AI and pathfinding | M | 2.1 |
| 10 | 3.1 Skeletal animation | L | none |
| 11 | 4.4 Sun, 4.1 audio, 4.2 SSAO, 4.3 probes | S to M each | none |
| 12 | 5.x tools and scale | mixed | 5.5 needs 1.4, 2.1, 3.1 |

## Out of scope for now

Networking and multiplayer, VR, full rigid-body physics (a kinematic prop
solver may come later), a visual scripting editor beyond the event rules in
2.1, and console ports.
