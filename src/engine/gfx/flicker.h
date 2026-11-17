#pragma once
// Failing light. Dropouts come from hashing the clock into short slots, so they are random
// but repeatable and never settle into a rhythm.
#include <cmath>
#include <cstdint>

namespace mythos {

inline uint32_t mixSlot(uint32_t h) {
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return h;
}

// The brightness multiplier of a lamp (`index` tells lamps apart) that is `flicker` restless
// (0 steady .. 1): mostly a slight waver, now and then a drop to near darkness.
inline double lampGain(double clockSeconds, int index, double flicker) {
    if (flicker <= 0) return 1.0;
    const uint32_t h = mixSlot(static_cast<uint32_t>(clockSeconds * 16.0) * 2654435761u + static_cast<uint32_t>(index) * 97u);
    double gain = 1.0;
    if ((h & 0xFFFF) / 65535.0 < flicker * 0.5) gain = 0.12;
    return gain * (0.93 + 0.07 * std::sin(clockSeconds * (9.0 + index) + index));
}

// Whether a torch is lit at this moment when it fails with the given restlessness.
inline bool torchLit(double clockSeconds, double flicker) {
    const uint32_t h = mixSlot(static_cast<uint32_t>(clockSeconds * 14.0) * 2654435761u);
    return (h & 0xFFFF) / 65535.0 >= flicker * 0.5;
}

}  // namespace mythos
