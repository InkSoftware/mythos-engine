#ifndef MYTHOS_ENGINE_GFX_RENDER_STATS_H_
#define MYTHOS_ENGINE_GFX_RENDER_STATS_H_

// What the renderer did in its last frame, for the debug overlay. Counted by
// `SceneRenderer`; plain data so the UI layer can show it.
namespace mythos {
struct RenderStats {
  // Draw calls and triangles of the main and mirror views, the sky, the
  // fullscreen passes and the glass, not the shadow cubes.
  int draw_calls = 0;
  int triangles = 0;
  // Draw calls of the shadow cube faces redrawn this frame, and how many faces.
  int shadow_draw_calls = 0;
  int shadow_faces = 0;
  // Point lights given, and how many of them (and the hand light) cast shadows.
  int lights = 0;
  int shadow_casters = 0;
  // Particle sprites drawn.
  int particles = 0;
  // The mirror view was drawn.
  bool mirror = false;
};
}  // namespace mythos

#endif
