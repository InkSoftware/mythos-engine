#pragma once
// Minimal RGBA pixel surface shared by the renderer, font and texture code.
// Pixels are 0xAARRGGBB. Alpha 0 means "transparent" for sprites.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mythos {

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline uint32_t rgba(int r, int g, int b, int a = 255) {
    return (static_cast<uint32_t>(clampi(a, 0, 255)) << 24) |
           (static_cast<uint32_t>(clampi(r, 0, 255)) << 16) |
           (static_cast<uint32_t>(clampi(g, 0, 255)) << 8) |
           static_cast<uint32_t>(clampi(b, 0, 255));
}

// Multiplies the colour channels by k (lighting). Result is opaque.
inline uint32_t shade(uint32_t c, double k) {
    int r = static_cast<int>(((c >> 16) & 0xFF) * k);
    int g = static_cast<int>(((c >> 8) & 0xFF) * k);
    int b = static_cast<int>((c & 0xFF) * k);
    return rgba(r, g, b, 255);
}

struct Surface {
    int w = 0;
    int h = 0;
    std::vector<uint32_t> px;

    Surface() = default;
    Surface(int width, int height, uint32_t fill = 0)
        : w(width), h(height), px(static_cast<size_t>(width) * static_cast<size_t>(height), fill) {}

    bool empty() const { return w <= 0 || h <= 0; }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }

    uint32_t& at(int x, int y) { return px[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]; }
    uint32_t at(int x, int y) const { return px[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]; }

    void put(int x, int y, uint32_t c) { if (inside(x, y)) at(x, y) = c; }
    void fill(uint32_t c) { for (auto& p : px) p = c; }
};

}  // namespace mythos
