#include "engine/gfx/normalmap.h"

#include <cmath>
#include <vector>

namespace mythos {

Surface makeNormalMap(const Surface& albedo, float strength) {
    if (albedo.empty()) return Surface();
    const int w = albedo.w, h = albedo.h;

    // Height = luma in 0..1. Transparent texels (sprites) count as flat ground.
    std::vector<float> height(static_cast<size_t>(w) * static_cast<size_t>(h), 0.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint32_t p = albedo.at(x, y);
            const float r = static_cast<float>((p >> 16) & 0xFF), g = static_cast<float>((p >> 8) & 0xFF),
                        b = static_cast<float>(p & 0xFF), a = static_cast<float>(p >> 24) / 255.0f;
            height[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f * a;
        }
    // Wrapped neighbour indices, worked out once instead of two modulos per sample.
    std::vector<int> xl(static_cast<size_t>(w)), xr(static_cast<size_t>(w));
    for (int x = 0; x < w; ++x) {
        xl[static_cast<size_t>(x)] = x == 0 ? w - 1 : x - 1;
        xr[static_cast<size_t>(x)] = x == w - 1 ? 0 : x + 1;
    }

    Surface out(w, h);
    for (int y = 0; y < h; ++y) {
        const float* rm = &height[static_cast<size_t>(y == 0 ? h - 1 : y - 1) * static_cast<size_t>(w)];
        const float* r0 = &height[static_cast<size_t>(y) * static_cast<size_t>(w)];
        const float* rp = &height[static_cast<size_t>(y == h - 1 ? 0 : y + 1) * static_cast<size_t>(w)];
        for (int x = 0; x < w; ++x) {
            const size_t l = static_cast<size_t>(xl[static_cast<size_t>(x)]), r = static_cast<size_t>(xr[static_cast<size_t>(x)]),
                         c = static_cast<size_t>(x);
            // Sobel, scaled so a full black-to-white step over one texel is a slope of 1.
            const float dx = ((rm[r] + 2.0f * r0[r] + rp[r]) - (rm[l] + 2.0f * r0[l] + rp[l])) / 8.0f;
            const float dy = ((rp[l] + 2.0f * rp[c] + rp[r]) - (rm[l] + 2.0f * rm[c] + rm[r])) / 8.0f;
            // Image rows run downwards but v runs up, so the y slope changes sign.
            float nx = -dx * strength, ny = dy * strength, nz = 1.0f;
            const float inv = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            nx *= inv;
            ny *= inv;
            nz *= inv;
            out.at(x, y) = rgba(static_cast<int>(std::lround((nx * 0.5f + 0.5f) * 255.0f)),
                                static_cast<int>(std::lround((ny * 0.5f + 0.5f) * 255.0f)),
                                static_cast<int>(std::lround((nz * 0.5f + 0.5f) * 255.0f)), 255);
        }
    }
    return out;
}

}  // namespace mythos
