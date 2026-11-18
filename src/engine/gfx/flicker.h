#ifndef MYTHOS_ENGINE_GFX_FLICKER_H_
#define MYTHOS_ENGINE_GFX_FLICKER_H_
// Failing light. Dropouts come from hashing the clock into short slots, so they
// are random but repeatable and never settle into a rhythm.
#include <cmath>
#include <cstdint>

namespace mythos {

inline uint32_t MixSlot(uint32_t h) {
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  return h;
}

// The brightness multiplier of a lamp (`index` tells lamps apart) that is
// `flicker` restless (0 steady .. 1): mostly a slight waver, now and then a
// drop to near darkness.
inline double LampGain(double clock_seconds, int index, double flicker) {
  if (flicker <= 0) return 1.0;
  const uint32_t h =
      MixSlot(static_cast<uint32_t>(clock_seconds * 16.0) * 2654435761u +
              static_cast<uint32_t>(index) * 97u);
  double gain = 1.0;
  if ((h & 0xFFFF) / 65535.0 < flicker * 0.5) gain = 0.12;
  return gain * (0.93 + 0.07 * std::sin(clock_seconds * (9.0 + index) + index));
}

// Whether a torch is lit at this moment when it fails with the given
// restlessness.
inline bool TorchLit(double clock_seconds, double flicker) {
  const uint32_t h =
      MixSlot(static_cast<uint32_t>(clock_seconds * 14.0) * 2654435761u);
  return (h & 0xFFFF) / 65535.0 >= flicker * 0.5;
}

}  // namespace mythos

#endif  // MYTHOS_ENGINE_GFX_FLICKER_H_
