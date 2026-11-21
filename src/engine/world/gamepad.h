#ifndef MYTHOS_ENGINE_WORLD_GAMEPAD_H_
#define MYTHOS_ENGINE_WORLD_GAMEPAD_H_

#include <algorithm>
#include <cmath>

namespace mythos {
// Headless arithmetic of a game controller's sticks, used by the platform's
// PollInput (the SDL part) and unit-tested without a device.

// Raw stick axis range of SDL (-32768..32767) to -1..1.
inline double StickAxisToUnit(const int raw) {
  return std::clamp(raw < 0 ? raw / 32768.0 : raw / 32767.0, -1.0, 1.0);
}

// A stick axis in -1..1 with a dead zone: 0 inside it, then rescaled so the
// output still reaches +-1 at the rim (no jump at the edge of the zone).
inline double StickDeadzone(const double v, const double dead_zone = 0.2) {
  const double a = std::fabs(v);
  if (a <= dead_zone) return 0.0;
  const double s = (std::min(a, 1.0) - dead_zone) / (1.0 - dead_zone);
  return v < 0 ? -s : s;
}

// How far a stick that is held at `v` (already past the dead zone) turns the
// view in `dt` seconds, in the pixels the mouse look takes. The curve is
// quadratic: a small push turns slowly and precisely, a full push quickly.
inline double StickLookPixels(const double v, const double dt,
                              const double pixels_per_second) {
  const double mag = std::fabs(v);
  const double d = mag * mag * pixels_per_second * dt;
  return v < 0 ? -d : d;
}
}

#endif
