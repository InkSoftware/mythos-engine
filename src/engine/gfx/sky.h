// The sky over time: where the sun or moon is, how bright and what colour its
// light is, and the colours of the sky, ambient light and fog, from a time of
// day in hours (0..24). Headless; the renderer reads a SkyLook each frame
// (CameraState::time_of_day).
#ifndef MYTHOS_ENGINE_GFX_SKY_H_
#define MYTHOS_ENGINE_GFX_SKY_H_

#include <array>

namespace mythos {
using Rgb = std::array<float, 3>;

struct SkyLook {
  // Unit vector toward the body that lights the world (the sun by day, the
  // moon by night); y is up, the sky turns east (+x) to west (-x) with z as
  // the south-north swing.
  std::array<float, 3> light_dir{0, 1, 0};
  // The direct light's colour, with its intensity folded in (linear).
  Rgb light_color{0, 0, 0};
  // Ambient (sky) light on the ground.
  Rgb ambient{0, 0, 0};
  // Sky colours overhead and at the horizon, and the haze the ground fades to.
  Rgb zenith{0, 0, 0}, horizon{0, 0, 0}, fog{0, 0, 0};
  // 0 (day) to 1 (night): how visible the stars are.
  float stars = 1;
  // 0 the moon, 1 the sun: what the disc in the sky is.
  float sun = 0;
  // Height of the sun above the horizon, -1 (below) to 1 (overhead); what
  // the day/night decision is made from.
  float sun_height = -1;
  // Brightness of the clouds (1 at night).
  float cloud_light = 1;
};

// The sky at `hour` (wraps, so 25 is 1 and -1 is 23). Continuous in the hour.
SkyLook SkyAt(double hour);

// The look the renderer uses when no time of day is set: the engine's night
// (cold moonlight, deep blue sky), exactly as it was before there was a day.
SkyLook NightLook();
}

#endif
