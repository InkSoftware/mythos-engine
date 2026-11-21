// Headless sound arithmetic: stereo panning and attenuation of a positional
// source, cue definitions and the random pick of a clip and pitch.
#ifndef MYTHOS_ENGINE_AUDIO_AUDIO_MATH_H_
#define MYTHOS_ENGINE_AUDIO_AUDIO_MATH_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/math.h"

namespace mythos {
// Per-channel linear gain, 0 (silent) and up.
struct StereoGain {
  float left = 0, right = 0;
};

// Gain of a point source heard by a listener on the XZ plane.
// Positions are in world units (Vec2::y is the world z) and `facing` is the
// listener's heading in radians, measured like atan2(dz, dx). Gain falls off as
// volume / (1 + falloff * d^2); it is zero inside `hush_radius` (when positive)
// or when `muted`. Panning is equal-power, narrowed to kPanWidth so a source
// directly to one side is not fully one-eared.
inline StereoGain PositionalGain(const Vec2& listener, const double facing,
                                 const Vec2& source, const double hush_radius,
                                 const bool muted = false,
                                 const double volume = 0.75,
                                 const double falloff = 0.5) {
  // Fraction of the full stereo field used; 1 would pan hard left and right.
  constexpr double kPanWidth = 0.75;

  const double dx = source.x - listener.x, dz = source.y - listener.y;
  const double distance = std::hypot(dx, dz);
  double gain = volume / (1.0 + falloff * distance * distance);
  if (hush_radius > 0 && distance < hush_radius) gain = 0.0;
  if (muted) gain = 0.0;
  const double bearing = std::atan2(dz, dx) - facing;
  const double right = 0.5 * (1.0 + kPanWidth * std::sin(bearing));
  return {.left = static_cast<float>(gain * std::sqrt(1.0 - right)),
          .right = static_cast<float>(gain * std::sqrt(right))};
}

// A named sound: the clips to choose from and how to play them.
struct SoundCue {
  // Clip names; one is picked at random per play.
  std::vector<std::string> clips;
  // Linear volume multiplier.
  float gain = 1.0f;
  // Playback rate multiplier.
  float pitch = 1.0f;
  // Randomize the pitch slightly on every play.
  bool jitter = false;
};

// Small deterministic linear congruential generator for audio variation. Not
// suitable for anything that needs quality or security.
class Rng {
 public:
  explicit Rng(const uint32_t seed = 0x9E3779B9u) : state_(seed) {}

  // Returns a value in [0, 1).
  float Next() {
    state_ = state_ * 1664525u + 1013904223u;
    return static_cast<float>(state_ >> 8) / 16777216.0f;
  }

 private:
  uint32_t state_;
};

// The outcome of PickCue: the index into SoundCue::clips and the pitch.
struct CuePick {
  size_t clip = 0;
  float pitch = 1.0f;
};

// Chooses a clip uniformly and a pitch for one play of `cue`. With jitter the
// pitch is scaled by a random factor in [0.92, 1.08).
inline CuePick PickCue(const SoundCue& cue, Rng& rng) {
  constexpr float kJitterMin = 0.92f;
  constexpr float kJitterRange = 0.16f;
  const float jitter = kJitterMin + kJitterRange * rng.Next();
  CuePick out;
  if (cue.clips.size() > 1) {
    out.clip = std::min(
        cue.clips.size() - 1,
        static_cast<size_t>(rng.Next() * static_cast<float>(cue.clips.size())));
  }
  out.pitch = cue.pitch * (cue.jitter ? jitter : 1.0f);
  return out;
}
}

#endif
