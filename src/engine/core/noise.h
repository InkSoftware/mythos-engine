#pragma once
// Deterministic hashing and value noise, for procedural textures.

#include <cstdint>

namespace mythos {

inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Value noise in [0,1) for a cell and a seed.
inline double hnoise(int x, int y, uint32_t seed) {
    uint32_t h = hash32(static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u ^ seed * 83492791u);
    return (h & 0xFFFFu) / 65535.0;
}

// Grain: small per-pixel noise, centred on 0, amplitude a.
inline int grain(int x, int y, uint32_t seed, int a) {
    return static_cast<int>((hnoise(x, y, seed) - 0.5) * 2.0 * a);
}

}  // namespace mythos
