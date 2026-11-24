#include "engine/gfx/sun_shadow.h"

#include <algorithm>
#include <cmath>

#include "engine/core/mat4.h"

namespace mythos {
namespace {
void Normalize(float* v) {
  const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (len < 1e-8f) {
    v[0] = 0;
    v[1] = 1;
    v[2] = 0;
    return;
  }
  for (int i = 0; i < 3; ++i) v[i] /= len;
}

void Cross(const float* a, const float* b, float* out) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}
}

SunShadow ComputeSunShadow(const float* const eye, const float* const to_light,
                           const float half_extent, const float depth_range,
                           const int map_size) {
  float dir[3] = {to_light[0], to_light[1], to_light[2]};
  Normalize(dir);
  // The light's axes: right and up in its view (the view looks along -dir).
  const float world_up[3] = {0, 1, 0};
  const float alt_up[3] = {0, 0, 1};
  const float* ref = std::fabs(dir[1]) > 0.99f ? alt_up : world_up;
  float right[3], up[3];
  Cross(ref, dir, right);  // ref x dir: the view's +x
  Normalize(right);
  Cross(dir, right, up);

  const float extent = std::max(0.1f, half_extent);
  const int size = std::max(1, map_size);
  SunShadow out{};
  out.texel = 2.0f * extent / static_cast<float>(size);

  // Snap the eye, in the light's x / y, to whole texels.
  const float ex = eye[0] * right[0] + eye[1] * right[1] + eye[2] * right[2];
  const float ey = eye[0] * up[0] + eye[1] * up[1] + eye[2] * up[2];
  const float ez = eye[0] * dir[0] + eye[1] * dir[1] + eye[2] * dir[2];
  const float sx = std::floor(ex / out.texel) * out.texel;
  const float sy = std::floor(ey / out.texel) * out.texel;
  const float sz =
      std::floor(ez / out.texel) * out.texel;  // depth too: a stable cache key
  float center[3];
  for (int i = 0; i < 3; ++i) {
    center[i] = right[i] * sx + up[i] * sy + dir[i] * sz;
  }
  const float half_depth = std::max(1.0f, depth_range) * 0.5f;
  float from[3];
  for (int i = 0; i < 3; ++i) from[i] = center[i] + dir[i] * half_depth;
  Mat4LookAt(from, center, up, out.view);
  Mat4Ortho(-extent, extent, -extent, extent, 0.0f, half_depth * 2.0f,
            out.proj);
  Mat4Multiply(out.proj, out.view, out.view_proj);
  return out;
}
}
