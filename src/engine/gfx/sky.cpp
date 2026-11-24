#include "engine/gfx/sky.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

// NOLINTBEGIN(modernize-use-std-numbers): colour values, not pi.
namespace mythos {
namespace {
struct Key {
  double hour;
  Rgb light, ambient, zenith, horizon, fog;
  float stars, cloud_light;
};

Rgb Mix(const Rgb& a, const Rgb& b, const float t) {
  return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t,
          a[2] + (b[2] - a[2]) * t};
}

// Midnight, before dawn, sunrise, morning, noon, evening, sunset, dusk,
// midnight again (the first and last are the same). Night is the engine's own
// look: moonlight (0.165, 0.209, 0.319), ambient (0.04, 0.052, 0.085).
const std::vector<Key>& Keys() {
  static const std::vector<Key> keys = {
      {0,
       {0.165f, 0.209f, 0.319f},
       {0.040f, 0.052f, 0.085f},
       {0.004f, 0.006f, 0.016f},
       {0.030f, 0.045f, 0.085f},
       {0.012f, 0.018f, 0.034f},
       1.0f,
       1.0f},
      {5,
       {0.165f, 0.209f, 0.319f},
       {0.040f, 0.052f, 0.085f},
       {0.004f, 0.006f, 0.016f},
       {0.030f, 0.045f, 0.085f},
       {0.012f, 0.018f, 0.034f},
       1.0f,
       1.0f},
      {6.5,
       {1.6f, 0.75f, 0.35f},
       {0.10f, 0.08f, 0.10f},
       {0.05f, 0.09f, 0.22f},
       {0.90f, 0.40f, 0.18f},
       {0.45f, 0.25f, 0.16f},
       0.3f,
       6.0f},
      {9,
       {2.6f, 2.2f, 1.7f},
       {0.18f, 0.24f, 0.34f},
       {0.10f, 0.25f, 0.62f},
       {0.45f, 0.58f, 0.78f},
       {0.40f, 0.50f, 0.65f},
       0.0f,
       12.0f},
      {12,
       {3.2f, 3.0f, 2.7f},
       {0.24f, 0.32f, 0.46f},
       {0.09f, 0.26f, 0.70f},
       {0.50f, 0.64f, 0.85f},
       {0.45f, 0.56f, 0.72f},
       0.0f,
       14.0f},
      {16.5,
       {2.6f, 2.2f, 1.7f},
       {0.18f, 0.24f, 0.34f},
       {0.10f, 0.25f, 0.62f},
       {0.45f, 0.58f, 0.78f},
       {0.40f, 0.50f, 0.65f},
       0.0f,
       12.0f},
      {18.5,
       {1.6f, 0.60f, 0.25f},
       {0.10f, 0.07f, 0.09f},
       {0.05f, 0.08f, 0.20f},
       {0.95f, 0.35f, 0.14f},
       {0.45f, 0.22f, 0.14f},
       0.3f,
       6.0f},
      {20,
       {0.165f, 0.209f, 0.319f},
       {0.040f, 0.052f, 0.085f},
       {0.004f, 0.006f, 0.016f},
       {0.030f, 0.045f, 0.085f},
       {0.012f, 0.018f, 0.034f},
       1.0f,
       1.0f},
      {24,
       {0.165f, 0.209f, 0.319f},
       {0.040f, 0.052f, 0.085f},
       {0.004f, 0.006f, 0.016f},
       {0.030f, 0.045f, 0.085f},
       {0.012f, 0.018f, 0.034f},
       1.0f,
       1.0f},
  };
  return keys;
}
}

SkyLook NightLook() {
  SkyLook look;
  // The direction the renderer always used for the moon.
  look.light_dir = {0.148f, 0.417f, -0.897f};
  look.light_color = {0.165f, 0.209f, 0.319f};
  look.ambient = {0.040f, 0.052f, 0.085f};
  look.zenith = {0.004f, 0.006f, 0.016f};
  look.horizon = {0.030f, 0.045f, 0.085f};
  look.fog = {0.012f, 0.018f, 0.034f};
  look.stars = 1;
  look.sun = 0;
  look.sun_height = -1;
  look.cloud_light = 1;
  return look;
}

SkyLook SkyAt(double hour) {
  hour = std::fmod(hour, 24.0);
  if (hour < 0) hour += 24.0;
  const auto& keys = Keys();
  size_t i = 1;
  while (i + 1 < keys.size() && keys[i].hour < hour) ++i;
  const Key& a = keys[i - 1];
  const Key& b = keys[i];
  const auto t = static_cast<float>((hour - a.hour) / (b.hour - a.hour));
  SkyLook look = NightLook();
  look.light_color = Mix(a.light, b.light, t);
  look.ambient = Mix(a.ambient, b.ambient, t);
  look.zenith = Mix(a.zenith, b.zenith, t);
  look.horizon = Mix(a.horizon, b.horizon, t);
  look.fog = Mix(a.fog, b.fog, t);
  look.stars = a.stars + (b.stars - a.stars) * t;
  look.cloud_light = a.cloud_light + (b.cloud_light - a.cloud_light) * t;

  // The sun rises at 6, is highest at noon and sets at 18; its path leans
  // south (+z). By night the moon keeps the engine's fixed place.
  constexpr double kPi = std::numbers::pi;
  const double day_angle = (hour - 6.0) / 12.0 * kPi;  // 0 at sunrise
  const double height = std::sin(day_angle);
  look.sun_height = static_cast<float>(std::clamp(height, -1.0, 1.0));
  if (hour >= 6.0 && hour <= 18.0) {
    const double el = std::max(0.02, height) * 1.2;  // radians-ish, kept low
    const double e = std::min(el, 1.45);
    look.light_dir = {static_cast<float>(std::cos(day_angle) * std::cos(e)),
                      static_cast<float>(std::sin(e)),
                      static_cast<float>(0.35 * std::cos(e))};
    const float len = std::sqrt(look.light_dir[0] * look.light_dir[0] +
                                look.light_dir[1] * look.light_dir[1] +
                                look.light_dir[2] * look.light_dir[2]);
    for (float& c : look.light_dir) c /= len;
    // The sun's own disc fades in as the moon's fades out around the horizon.
    look.sun = 1;
  } else {
    look.sun = 0;
  }
  return look;
}
}
// NOLINTEND(modernize-use-std-numbers)
