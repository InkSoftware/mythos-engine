#include "engine/gfx/normalmap.h"

#include <cmath>
#include <vector>

namespace mythos {
namespace {
// Height field of `albedo`, 0..1 row by row: perceived luma (Rec. 601 weights)
// scaled by alpha, so transparent texels are low.
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
          (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f * a;
    }
  }
  return height;
}

// Maps a unit-vector component in [-1, 1] to a byte.
int NormalToByte(const float component) {
  return static_cast<int>(std::lround((component * 0.5f + 0.5f) * 255.0f));
}
}

Surface MakeNormalMap(const Surface& albedo, const float strength) {
  if (albedo.Empty()) return {};
  const int w = albedo.w, h = albedo.h;

  // Neighbor indices wrap, because the texture repeats.
  const std::vector<float> height = LumaHeights(albedo);

  std::vector<int> left_of(static_cast<size_t>(w)),
      right_of(static_cast<size_t>(w));
  for (int x = 0; x < w; ++x) {
    left_of[static_cast<size_t>(x)] = x == 0 ? w - 1 : x - 1;
    right_of[static_cast<size_t>(x)] = x == w - 1 ? 0 : x + 1;
  }

  Surface out(w, h);
  for (int y = 0; y < h; ++y) {
    const float* row_above =
        &height[static_cast<size_t>(y == 0 ? h - 1 : y - 1) *
                static_cast<size_t>(w)];
    const float* row_here =
        &height[static_cast<size_t>(y) * static_cast<size_t>(w)];
    const float* row_below =
        &height[static_cast<size_t>(y == h - 1 ? 0 : y + 1) *
                static_cast<size_t>(w)];
    for (int x = 0; x < w; ++x) {
      const auto l = static_cast<size_t>(left_of[static_cast<size_t>(x)]),
                 r = static_cast<size_t>(right_of[static_cast<size_t>(x)]),
                 c = static_cast<size_t>(x);

      // Sobel filter: the slope of the height along x and along y, divided by 8
      // so that a unit step in height reads as a slope of about 1 per pixel.
      const float dx = ((row_above[r] + 2.0f * row_here[r] + row_below[r]) -
                        (row_above[l] + 2.0f * row_here[l] + row_below[l])) /
                       8.0f;
      const float dy = ((row_below[l] + 2.0f * row_below[c] + row_below[r]) -
                        (row_above[l] + 2.0f * row_above[c] + row_above[r])) /
                       8.0f;

      // The normal leans away from the rising side. Columns grow with x, so
      // nx = -dx; y points up the image while dy is measured downwards, so
      // the two signs cancel and ny = +dy.
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
}
