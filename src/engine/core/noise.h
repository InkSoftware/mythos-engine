// Stateless integer hashing for repeatable noise (texture grain, per-cell
// variation).
#ifndef MYTHOS_ENGINE_CORE_NOISE_H_
#define MYTHOS_ENGINE_CORE_NOISE_H_

#include <cstdint>

namespace mythos {
// Integer finalizer with good avalanche (lowbias32).
inline uint32_t Hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

// Deterministic noise in [0, 1] for the lattice point (x, y) and `seed`.
inline double Hnoise(const int x, const int y, const uint32_t seed) {
  const uint32_t hash =
      Hash32(static_cast<uint32_t>(x) * 73856093u ^
             static_cast<uint32_t>(y) * 19349663u ^ seed * 83492791u);
  return (hash & 0xFFFFu) / 65535.0;
}

// Integer noise in about [-amplitude, amplitude], centerd on zero.
inline int Grain(const int x, const int y, const uint32_t seed,
                 const int amplitude) {
  return static_cast<int>((Hnoise(x, y, seed) - 0.5) * 2.0 * amplitude);
}
}

#endif
