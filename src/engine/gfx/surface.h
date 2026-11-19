#ifndef MYTHOS_ENGINE_GFX_SURFACE_H_
#define MYTHOS_ENGINE_GFX_SURFACE_H_
// Minimal RGBA pixel surface shared by the renderer, font and texture code.
// Pixels are 0xAARRGGBB. Alpha 0 means "transparent" for sprites.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mythos {

inline int Clampi(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

inline uint32_t Rgba(int r, int g, int b, int a = 255) {
  return (static_cast<uint32_t>(Clampi(a, 0, 255)) << 24) |
         (static_cast<uint32_t>(Clampi(r, 0, 255)) << 16) |
         (static_cast<uint32_t>(Clampi(g, 0, 255)) << 8) |
         static_cast<uint32_t>(Clampi(b, 0, 255));
}

struct Surface {
  int w = 0;
  int h = 0;
  std::vector<uint32_t> px;

  Surface() = default;
  Surface(int width, int height, uint32_t fill = 0)
      : w(width),
        h(height),
        px(static_cast<size_t>(width) * static_cast<size_t>(height), fill) {}

  bool Empty() const { return w <= 0 || h <= 0; }
  bool Inside(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }

  uint32_t& At(int x, int y) {
    return px[static_cast<size_t>(y) * static_cast<size_t>(w) +
              static_cast<size_t>(x)];
  }
  uint32_t At(int x, int y) const {
    return px[static_cast<size_t>(y) * static_cast<size_t>(w) +
              static_cast<size_t>(x)];
  }

  void Put(int x, int y, uint32_t c) {
    if (Inside(x, y)) At(x, y) = c;
  }
  void Fill(uint32_t c) {
    for (auto& p : px) p = c;
  }
};

}  // namespace mythos

#endif  // MYTHOS_ENGINE_GFX_SURFACE_H_
