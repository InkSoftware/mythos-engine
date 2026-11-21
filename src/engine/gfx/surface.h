#ifndef MYTHOS_ENGINE_GFX_SURFACE_H_
#define MYTHOS_ENGINE_GFX_SURFACE_H_

#include <cstddef>
#include <cstdint>
#include <vector>

// Minimal RGBA pixel surface shared by the renderer, the font and the texture
// code. Pixels are 0xAARRGGBB; alpha 0 means transparent for sprites.
namespace mythos {
// Clamps `v` to [lo, hi].
inline int Clampi(const int v, const int lo, const int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Packs 0..255 channels (each clamped) into a 0xAARRGGBB pixel.
inline uint32_t Rgba(const int r, const int g, const int b, const int a = 255) {
  return (static_cast<uint32_t>(Clampi(a, 0, 255)) << 24) |
         (static_cast<uint32_t>(Clampi(r, 0, 255)) << 16) |
         (static_cast<uint32_t>(Clampi(g, 0, 255)) << 8) |
         static_cast<uint32_t>(Clampi(b, 0, 255));
}

// A width by height grid of pixels stored row by row, top row first. Plain
// value type; copying copies the pixels.
struct Surface {
  int w = 0;
  int h = 0;
  std::vector<uint32_t> px;

  Surface() = default;

  Surface(const int width, const int height, const uint32_t fill = 0)
      : w(width),
        h(height),
        px(static_cast<size_t>(width) * static_cast<size_t>(height), fill) {}

  // True when the surface has no pixels.
  [[nodiscard]] bool Empty() const { return w <= 0 || h <= 0; }

  [[nodiscard]] bool Inside(const int x, const int y) const {
    return x >= 0 && y >= 0 && x < w && y < h;
  }

  // Pixel access. The coordinates must be inside the surface; use Inside() or
  // Put() when they may not be.
  uint32_t& At(const int x, const int y) {
    return px[static_cast<size_t>(y) * static_cast<size_t>(w) +
              static_cast<size_t>(x)];
  }

  [[nodiscard]] uint32_t At(const int x, const int y) const {
    return px[static_cast<size_t>(y) * static_cast<size_t>(w) +
              static_cast<size_t>(x)];
  }

  // Writes a pixel, silently ignoring coordinates outside the surface.
  void Put(const int x, const int y, const uint32_t c) {
    if (Inside(x, y)) At(x, y) = c;
  }

  void Fill(const uint32_t c) {
    for (auto& p : px) p = c;
  }
};
}

#endif
