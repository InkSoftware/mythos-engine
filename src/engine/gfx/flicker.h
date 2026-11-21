#ifndef MYTHOS_ENGINE_GFX_FLICKER_H_
#define MYTHOS_ENGINE_GFX_FLICKER_H_

#include <cmath>
#include <cstdint>

namespace mythos {
// Failing-light helpers. Dropouts come from hashing the clock into short time
// slots, so they are random but repeatable and never settle into a rhythm.
//
// Integer hash (a cheap avalanche mix) used to turn a time slot into pseudo-
// random bits.
inline uint32_t MixSlot(uint32_t h) {
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  return h;
}

// Brightness multiplier of lamp `index` at `clock_seconds`. `flicker` is the
// restlessness, 0 (steady, always returns 1) to 1. Mostly a slight waver of
// about 7 percent; in a random 16 Hz slot it drops to 0.12 (near darkness) with
// probability flicker / 2. `index` offsets both the hash and the waver so lamps
// do not flicker in step.
inline double LampGain(double clock_seconds, int index, double flicker) {
  if (flicker <= 0) return 1.0;
  const uint32_t h =
      MixSlot(static_cast<uint32_t>(clock_seconds * 16.0) * 2654435761u +
              static_cast<uint32_t>(index) * 97u);
  double gain = 1.0;
  if ((h & 0xFFFF) / 65535.0 < flicker * 0.5) gain = 0.12;
  return gain * (0.93 + 0.07 * std::sin(clock_seconds * (9.0 + index) + index));
}

// Whether a torch is lit at `clock_seconds` when it fails with the given
// `flicker` restlessness (0..1): it is out in a random 14 Hz slot with
// probability flicker / 2.
inline bool TorchLit(double clock_seconds, double flicker) {
  const uint32_t h =
      MixSlot(static_cast<uint32_t>(clock_seconds * 14.0) * 2654435761u);
  return (h & 0xFFFF) / 65535.0 >= flicker * 0.5;
}
}

#endif
