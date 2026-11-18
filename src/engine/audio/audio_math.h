#pragma once
// The arithmetic of sound in a world, with no audio device: where a source sits relative to
// the listener, how cues pick their clips, and a small random generator for variation.
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/math.h"

namespace mythos {

struct StereoGain {
    float left = 0, right = 0;
};

// The stereo gains of a positional source heard by a listener at `listener` facing `facing`
// (radians; 0 is +x). Loudness is volume / (1 + falloff * distance^2): `falloff` 0.5 is a source you
// have to be near to hear (a ticking clock); a smaller one carries (a voice from a radio, heard
// across the house, much softer). The pan is soft (never hard to one side), a source with
// hushRadius > 0 falls silent closer than that, and `muted` silences it outright.
inline StereoGain positionalGain(const Vec2& listener, double facing, const Vec2& source, double hushRadius,
                                 bool muted = false, double volume = 0.75, double falloff = 0.5) {
    constexpr double kPanWidth = 0.75;  // 1 would pan hard to one side at 90 degrees; less keeps both ears in play
    const double dx = source.x - listener.x, dz = source.y - listener.y;
    const double distance = std::hypot(dx, dz);
    double gain = volume / (1.0 + falloff * distance * distance);
    if (hushRadius > 0 && distance < hushRadius) gain = 0.0;
    if (muted) gain = 0.0;
    const double bearing = std::atan2(dz, dx) - facing;
    const double right = 0.5 * (1.0 + kPanWidth * std::sin(bearing));  // 0 all left .. 1 all right; equal-power pan below
    return {static_cast<float>(gain * std::sqrt(1.0 - right)), static_cast<float>(gain * std::sqrt(right))};
}

// A sound event: one of several clips, how loud, and how it varies.
struct SoundCue {
    std::vector<std::string> clips;
    float gain = 1.0f;
    float pitch = 1.0f;
    bool jitter = false;  // vary the pitch a little each time (footsteps)
};

// A cheap deterministic [0, 1) generator, for step variation and the like.
class Rng {
public:
    explicit Rng(uint32_t seed = 0x9E3779B9u) : state_(seed) {}
    float next() {
        // Numerical Recipes linear congruential generator; the top 24 bits fill a float's mantissa exactly.
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<float>(state_ >> 8) / 16777216.0f;
    }

private:
    uint32_t state_;
};

// The clip a cue plays this time (the first when it has one) and its pitch, drawing from rng the
// way the footsteps always have: a pitch draw first, then a clip draw only when there is a choice.
struct CuePick {
    size_t clip = 0;
    float pitch = 1.0f;
};
inline CuePick pickCue(const SoundCue& cue, Rng& rng) {
    constexpr float kJitterMin = 0.92f;   // pitch varies within +-8% of the cue's pitch
    constexpr float kJitterRange = 0.16f;
    const float jitter = kJitterMin + kJitterRange * rng.next();
    CuePick out;
    if (cue.clips.size() > 1)
        out.clip = std::min(cue.clips.size() - 1, static_cast<size_t>(rng.next() * static_cast<float>(cue.clips.size())));
    out.pitch = cue.pitch * (cue.jitter ? jitter : 1.0f);
    return out;
}

}  // namespace mythos
