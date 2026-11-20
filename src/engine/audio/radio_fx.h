#ifndef MYTHOS_ENGINE_AUDIO_RADIO_FX_H_
#define MYTHOS_ENGINE_AUDIO_RADIO_FX_H_
// The sound of a voice coming out of an FM radio that is not quite tuned: a
// narrow band (a small cloth speaker), a little drive, a constant hiss, a slow
// fade and warble of the carrier (multipath), and now and then a glitch:
//   dropout   the signal falls away into a crackle,
//   squeal    a tuning whistle sweeps through,
//   stutter   the last few dozen milliseconds loop,
//   wash      a burst of noise drowns the voice.
// Headless arithmetic, one sample at a time, so it can run inside an audio
// callback and be tested without a device. Deterministic for a seed.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mythos {

class RadioFm {
 public:
  enum class Glitch { kNone, kDropout, kSqueal, kStutter, kWash };

  // `intensity` 0..1: how often the glitches come and how hard the hiss is.
  explicit RadioFm(int sample_rate, uint32_t seed = 1, float intensity = 0.6f)
      : rate_(std::max(8000, sample_rate)),
        rng_(seed ? seed : 1u),
        intensity_(std::clamp(intensity, 0.0f, 1.0f)),
        delay_(static_cast<size_t>(rate_ / 2), 0.0f) {
    lp_a_ = OnePole(3400.0f);
    hp_a_ = OnePole(260.0f);
    ScheduleNext();
  }

  Glitch Current() const { return glitch_; }

  float Process(float in) {
    const float t = static_cast<float>(sample_) / static_cast<float>(rate_);
    ++sample_;

    // The carrier: a slow fade (the station drifting) and a quick warble.
    const float fade = 0.80f + 0.20f * std::sin(6.2831853f * 0.31f * t + 1.3f) *
                                   std::sin(6.2831853f * 0.07f * t);
    const float warble_ms = 0.35f + 0.5f * intensity_;
    const float warble =
        warble_ms * 0.001f * static_cast<float>(rate_) *
        (0.5f + 0.5f * std::sin(6.2831853f * 5.3f * t + 0.6f * std::sin(t)));

    // The line in, through a short delay whose length wobbles (the warble).
    delay_[write_] = in;
    const float read_at = static_cast<float>(write_) +
                          static_cast<float>(delay_.size()) - (4.0f + warble);
    const size_t i0 = static_cast<size_t>(read_at) % delay_.size();
    const size_t i1 = (i0 + 1) % delay_.size();
    const float frac = read_at - std::floor(read_at);
    float x = delay_[i0] * (1.0f - frac) + delay_[i1] * frac;

    // A glitch in progress changes what is heard.
    float noise = White();
    float gain = fade;
    float add = 0.0f;
    if (remaining_ > 0) {
      const float k = static_cast<float>(remaining_) /
                      static_cast<float>(length_);  // 1 -> 0 over the glitch
      switch (glitch_) {
        case Glitch::kDropout:
          gain *= 0.04f;
          add = noise * 0.5f * (0.4f + 0.6f * k) * Crackle();
          break;
        case Glitch::kSqueal: {
          const float f = sweep_from_ + (sweep_to_ - sweep_from_) * (1.0f - k);
          phase_ += 6.2831853f * f / static_cast<float>(rate_);
          if (phase_ > 6.2831853f) phase_ -= 6.2831853f;
          gain *= 0.45f;
          add = std::sin(phase_) * 0.22f * std::sin(3.14159265f * (1.0f - k));
          break;
        }
        case Glitch::kStutter: {
          // Hold a short window of the line and loop it.
          x = delay_[(stutter_start_ + held_ % stutter_span_) % delay_.size()];
          ++held_;
          break;
        }
        case Glitch::kWash:
          gain *= 0.35f;
          add = noise * 0.32f;
          break;
        case Glitch::kNone:
          break;
      }
      if (--remaining_ == 0) {
        glitch_ = Glitch::kNone;
        ScheduleNext();
      }
    } else if (--until_next_ <= 0) {
      StartGlitch();
    }
    write_ = (write_ + 1) % delay_.size();

    // The little speaker: band-limited, a touch of drive, and always the hiss.
    float y = x * gain + add;
    lp1_ += lp_a_ * (y - lp1_);
    lp2_ += lp_a_ * (lp1_ - lp2_);
    hp_ += hp_a_ * (lp2_ - hp_);
    y = lp2_ - hp_;
    y = std::tanh(y * (1.6f + 1.2f * intensity_)) * 0.8f;
    y += noise * (0.012f + 0.03f * intensity_);
    return y;
  }

 private:
  float OnePole(float hz) const {
    return 1.0f - std::exp(-6.2831853f * hz / static_cast<float>(rate_));
  }
  uint32_t Next() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
  }
  float Unit() { return (Next() & 0xFFFFFF) / 16777216.0f; }
  float White() { return Unit() * 2.0f - 1.0f; }
  // Crackle: the noise comes in uneven clumps.
  float Crackle() { return Unit() < 0.35f ? 1.0f : 0.15f; }

  void ScheduleNext() {
    // Seconds to the next glitch: shorter the more intense the setting.
    const float lo = 1.4f - 0.9f * intensity_, hi = 4.8f - 3.0f * intensity_;
    until_next_ = static_cast<int64_t>((lo + (hi - lo) * Unit()) *
                                       static_cast<float>(rate_));
  }
  void StartGlitch() {
    const float r = Unit();
    float seconds = 0.1f;
    if (r < 0.34f) {
      glitch_ = Glitch::kDropout;
      seconds = 0.06f + 0.16f * Unit();
    } else if (r < 0.58f) {
      glitch_ = Glitch::kSqueal;
      seconds = 0.14f + 0.22f * Unit();
      const bool up = Unit() < 0.5f;
      sweep_from_ = up ? 500.0f : 2600.0f;
      sweep_to_ = up ? 2600.0f : 500.0f;
    } else if (r < 0.82f) {
      glitch_ = Glitch::kStutter;
      seconds = 0.15f + 0.2f * Unit();
      stutter_span_ = static_cast<size_t>((0.04f + 0.05f * Unit()) *
                                          static_cast<float>(rate_));
      held_ = 0;
      stutter_start_ = (write_ + delay_.size() - stutter_span_) % delay_.size();
    } else {
      glitch_ = Glitch::kWash;
      seconds = 0.2f + 0.3f * Unit();
    }
    length_ = remaining_ =
        std::max<int64_t>(1, static_cast<int64_t>(seconds * rate_));
  }

  int rate_;
  uint32_t rng_;
  float intensity_;
  std::vector<float> delay_;
  size_t write_ = 0;
  uint64_t sample_ = 0;
  int64_t until_next_ = 0, remaining_ = 0, length_ = 1;
  Glitch glitch_ = Glitch::kNone;
  float phase_ = 0, sweep_from_ = 500, sweep_to_ = 2600;
  size_t stutter_span_ = 2048, held_ = 0, stutter_start_ = 0;
  float lp_a_ = 0.4f, hp_a_ = 0.04f, lp1_ = 0, lp2_ = 0, hp_ = 0;
};

}  // namespace mythos

#endif  // MYTHOS_ENGINE_AUDIO_RADIO_FX_H_
