// The matrices of a directional light's shadow map (the sun or the moon): an
// orthographic box centred on the viewer, looking along the light, with its
// centre snapped to whole shadow-map texels so the shadows do not shimmer as
// the viewer moves. Headless; `SceneRenderer` draws the map with them.
#ifndef MYTHOS_ENGINE_GFX_SUN_SHADOW_H_
#define MYTHOS_ENGINE_GFX_SUN_SHADOW_H_

namespace mythos {
struct SunShadow {
  // Column-major, like core/mat4.h: the light's view, its projection and the
  // product (world to shadow clip space).
  float view[16];
  float proj[16];
  float view_proj[16];
  // The world size of one shadow-map texel, in metres.
  float texel = 0;
};

// `eye` is the viewer, `to_light` the direction toward the light (need not be
// unit length, must not be zero). The box is `half_extent` metres each side
// of the eye across the light's view and `depth_range` metres deep (centred on
// the eye along the light). `map_size` is the shadow map's side in texels.
SunShadow ComputeSunShadow(const float* eye, const float* to_light,
                           float half_extent, float depth_range, int map_size);
}

#endif
