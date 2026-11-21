// Per-sample FM radio effect for voice lines: slow fading, pitch warble, band-
// limiting and saturation, with random glitches (dropouts, squeals, stutters,
// static washes) scheduled at intervals.
#ifndef MYTHOS_ENGINE_AUDIO_RADIO_FX_H_
#define MYTHOS_ENGINE_AUDIO_RADIO_FX_H_

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace mythos {
// Mono streaming filter. Feed it one sample at a time with Process(); it keeps
// all state (delay line, noise generator, glitch schedule) internally and
// allocates only in the constructor. Deterministic for a given seed. Not
// thread-safe.
class RadioFm {
 public:
  // The kind of glitch in progress.
  enum class Glitch { kNone, kDropout, kSqueal, kStutter, kWash };

  // `sample_rate` is clamped to at least 8 kHz. `seed` of 0 is replaced by 1
  // (xorshift cannot start at 0). `intensity` is clamped to [0, 1] and scales
  // glitch frequency, warble, drive and hiss. The delay line holds half a
  // second.
  explicit RadioFm(const int sample_rate, const uint32_t seed = 1,
                   const float intensity = 0.6f)
      : rate_(std::max(8000, sample_rate)),
        rng_(seed ? seed : 1u),
        intensity_(std::clamp(intensity, 0.0f, 1.0f)),
        delay_(static_cast<size_t>(rate_ / 2), 0.0f) {
    // Telephone-like band, roughly 260 Hz to 3.4 kHz.
    lp_a_ = OnePole(3400.0f);
    hp_a_ = OnePole(260.0f);
    ScheduleNext();
  }

  // The glitch being applied now, kNone between glitches.
  [[nodiscard]] Glitch Current() const { return glitch_; }

  // Returns the processed version of one input sample.
  float Process(const float in) {
    const float t = static_cast<float>(sample_) / static_cast<float>(rate_);
    ++sample_;

    // Slow, shallow fading of the signal strength, as from a weak carrier.
    const float fade = 0.80f + 0.20f * std::sin(6.2831853f * 0.31f * t + 1.3f) *
                                   std::sin(6.2831853f * 0.07f * t);
    // Warble: a short modulated delay read, which bends the pitch like a
    // drifting tuner.
    const float warble_ms = 0.35f + 0.5f * intensity_;
    const float warble =
        warble_ms * 0.001f * static_cast<float>(rate_) *
        (0.5f + 0.5f * std::sin(6.2831853f * 5.3f * t + 0.6f * std::sin(t)));

    delay_[write_] = in;
    const float read_at = static_cast<float>(write_) +
                          static_cast<float>(delay_.size()) - (4.0f + warble);
    const size_t i0 = static_cast<size_t>(read_at) % delay_.size();
    const size_t i1 = (i0 + 1) % delay_.size();
    const float frac = read_at - std::floor(read_at);
    float x = delay_[i0] * (1.0f - frac) + delay_[i1] * frac;

    // Glitches change the gain, add a generated signal, or replace the sample.
    const float noise = White();
    float gain = fade;
    float add = 0.0f;
    if (remaining_ > 0) {
      const float k =
          static_cast<float>(remaining_) / static_cast<float>(length_);
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
          add = std::sin(phase_) * 0.22f *
                std::sin(std::numbers::pi_v<float> * (1.0f - k));
          break;
        }
        // Replays a short slice of the delay line over and over.
        case Glitch::kStutter: {
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

    // Two cascaded low-passes and a high-pass shape the band, then tanh adds
    // soft clipping and a hiss floor.
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
  // Coefficient of a one-pole filter with cutoff `hz`.
  [[nodiscard]] float OnePole(const float hz) const {
    return 1.0f - std::exp(-6.2831853f * hz / static_cast<float>(rate_));
  }

  // Xorshift32 step.
  uint32_t Next() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
  }

  float Unit() { return static_cast<float>(Next() & 0xFFFFFF) / 16777216.0f; }
  float White() { return Unit() * 2.0f - 1.0f; }
  // Randomly gates the dropout noise into crackling bursts.
  float Crackle() { return Unit() < 0.35f ? 1.0f : 0.15f; }

  // Picks the quiet gap before the next glitch; higher intensity means shorter
  // gaps.
  void ScheduleNext() {
    const float lo = 1.4f - 0.9f * intensity_, hi = 4.8f - 3.0f * intensity_;
    until_next_ = static_cast<int64_t>((lo + (hi - lo) * Unit()) *
                                       static_cast<float>(rate_));
  }

  // Chooses a glitch kind by weighted odds and sets its length and parameters.
  void StartGlitch() {
    const float r = Unit();
    float seconds;
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
    length_ = remaining_ = std::max<int64_t>(
        1, static_cast<int64_t>(seconds * static_cast<float>(rate_)));
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
}

#endif
