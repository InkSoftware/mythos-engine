#ifndef MYTHOS_ENGINE_CORE_NOISE_H_
#define MYTHOS_ENGINE_CORE_NOISE_H_
// Deterministic hashing and value noise, for procedural textures.

#include <cstdint>

namespace mythos {

inline uint32_t Hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

// Value noise in [0,1) for a cell and a seed.
inline double Hnoise(int x, int y, uint32_t seed) {
  const uint32_t hash =
      Hash32(static_cast<uint32_t>(x) * 73856093u ^
             static_cast<uint32_t>(y) * 19349663u ^ seed * 83492791u);
  return (hash & 0xFFFFu) / 65535.0;
}

// Grain: small per-pixel noise, centred on 0, within +-amplitude.
inline int Grain(int x, int y, uint32_t seed, int amplitude) {
  return static_cast<int>((Hnoise(x, y, seed) - 0.5) * 2.0 * amplitude);
}

}  // namespace mythos

#endif  // MYTHOS_ENGINE_CORE_NOISE_H_
