#include "engine/gfx/normalmap.h"

#include <cmath>
#include <vector>

namespace mythos {

namespace {

// Height = luma in 0..1. Transparent texels (sprites) count as flat ground.
std::vector<float> LumaHeights(const Surface& albedo) {
  const int width = albedo.w, rows = albedo.h;
  std::vector<float> height(
      static_cast<size_t>(width) * static_cast<size_t>(rows), 0.0f);
  for (int y = 0; y < rows; ++y) {
    for (int x = 0; x < width; ++x) {
      const uint32_t pixel = albedo.At(x, y);
      const float r = static_cast<float>((pixel >> 16) & 0xFF),
                  g = static_cast<float>((pixel >> 8) & 0xFF),
                  b = static_cast<float>(pixel & 0xFF),
                  a = static_cast<float>(pixel >> 24) / 255.0f;
      height[static_cast<size_t>(y) * static_cast<size_t>(width) +
             static_cast<size_t>(x)] =
          (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f * a;  // Rec. 601 luma
    }
  }
  return height;
}

// A unit normal's component (-1..1) as a colour byte.
int NormalToByte(float component) {
  return static_cast<int>(std::lround((component * 0.5f + 0.5f) * 255.0f));
}

}  // namespace

Surface MakeNormalMap(const Surface& albedo, float strength) {
  if (albedo.Empty()) return {};
  const int w = albedo.w, h = albedo.h;

  const std::vector<float> height = LumaHeights(albedo);
  // Wrapped neighbour indices, worked out once instead of two modulos per
  // sample.
  std::vector<int> left_of(static_cast<size_t>(w)),
      right_of(static_cast<size_t>(w));
  for (int x = 0; x < w; ++x) {
    left_of[static_cast<size_t>(x)] = x == 0 ? w - 1 : x - 1;
    right_of[static_cast<size_t>(x)] = x == w - 1 ? 0 : x + 1;
  }

  Surface out(w, h);
  for (int y = 0; y < h; ++y) {
    // The three rows of the Sobel kernel (the texture wraps top to bottom).
    const float* row_above =
        &height[static_cast<size_t>(y == 0 ? h - 1 : y - 1) *
                static_cast<size_t>(w)];
    const float* row_here =
        &height[static_cast<size_t>(y) * static_cast<size_t>(w)];
    const float* row_below =
        &height[static_cast<size_t>(y == h - 1 ? 0 : y + 1) *
                static_cast<size_t>(w)];
    for (int x = 0; x < w; ++x) {
      const size_t l = static_cast<size_t>(left_of[static_cast<size_t>(x)]),
                   r = static_cast<size_t>(right_of[static_cast<size_t>(x)]),
                   c = static_cast<size_t>(x);
      // Sobel, scaled so a full black-to-white step over one texel is a slope
      // of 1.
      const float dx = ((row_above[r] + 2.0f * row_here[r] + row_below[r]) -
                        (row_above[l] + 2.0f * row_here[l] + row_below[l])) /
                       8.0f;
      const float dy = ((row_below[l] + 2.0f * row_below[c] + row_below[r]) -
                        (row_above[l] + 2.0f * row_above[c] + row_above[r])) /
                       8.0f;
      // Image rows run downwards but v runs up, so the y slope changes sign.
      float nx = -dx * strength, ny = dy * strength, nz = 1.0f;
      const float inverse_length =
          1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
      nx *= inverse_length;
      ny *= inverse_length;
      nz *= inverse_length;
      out.At(x, y) =
          Rgba(NormalToByte(nx), NormalToByte(ny), NormalToByte(nz), 255);
    }
  }
  return out;
}

}  // namespace mythos
