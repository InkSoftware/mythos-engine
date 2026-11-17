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
    auto H = [&](int x, int y) {
        x = ((x % w) + w) % w;
        y = ((y % h) + h) % h;
        return height[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
    };

    Surface out(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            // Sobel, scaled so a full black-to-white step over one texel is a slope of 1.
            const float dx = ((H(x + 1, y - 1) + 2.0f * H(x + 1, y) + H(x + 1, y + 1)) -
                              (H(x - 1, y - 1) + 2.0f * H(x - 1, y) + H(x - 1, y + 1))) / 8.0f;
            const float dy = ((H(x - 1, y + 1) + 2.0f * H(x, y + 1) + H(x + 1, y + 1)) -
                              (H(x - 1, y - 1) + 2.0f * H(x, y - 1) + H(x + 1, y - 1))) / 8.0f;
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
    return out;
}

}  // namespace mythos
