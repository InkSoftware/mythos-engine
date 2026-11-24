#include "engine/audio/reverb.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace mythos {
namespace {
// Delay lengths in milliseconds: mutually prime-ish so the echoes do not line
// up; the right channel is a little longer for width.
constexpr std::array<double, 4> kCombMs = {29.7, 37.1, 41.1, 43.7};
constexpr std::array<double, 2> kAllPassMs = {5.0, 1.7};
constexpr double kRightOffsetMs = 0.7;
constexpr float kDamping = 0.25f;
constexpr float kAllPassGain = 0.5f;
// How fast the send and the decay follow what was set, per second.
constexpr double kEasePerSecond = 8.0;

size_t Samples(const double ms, const double rate) {
  return std::max<size_t>(1,
                          static_cast<size_t>(std::lround(ms * 0.001 * rate)));
}
}

void Reverb::Prepare(const double sample_rate) {
  rate_ = std::max(8000.0, sample_rate);
  for (size_t i = 0; i < kCombMs.size(); ++i) {
    comb_l_[i] = {std::vector<float>(Samples(kCombMs[i], rate_), 0.0f), 0, 0};
    comb_r_[i] = {
        std::vector<float>(Samples(kCombMs[i] + kRightOffsetMs, rate_), 0.0f),
        0, 0};
  }
  for (size_t i = 0; i < kAllPassMs.size(); ++i) {
    all_l_[i] = {std::vector<float>(Samples(kAllPassMs[i], rate_), 0.0f), 0};
    all_r_[i] = {std::vector<float>(
                     Samples(kAllPassMs[i] + kRightOffsetMs, rate_), 0.0f),
                 0};
  }
  send_ = target_send_ = 0;
  tail_ = false;
  UpdateFeedback();
}

void Reverb::Set(const double send, const double decay_seconds) {
  target_send_ = std::clamp(send, 0.0, 1.0);
  target_decay_ = std::clamp(decay_seconds, 0.05, 10.0);
}

// A comb's feedback so that its loop loses 60 dB in `decay_` seconds:
// g = 10^(-3 * delay / decay).
void Reverb::UpdateFeedback() {
  for (size_t i = 0; i < kCombMs.size(); ++i) {
    feedback_[i] =
        static_cast<float>(std::pow(10.0, -3.0 * kCombMs[i] * 0.001 / decay_));
  }
}

void Reverb::Process(float* const out, const int frames) {
  ProcessSend(out, out, frames);
}

void Reverb::ProcessSend(const float* const send, float* const out,
                         const int frames) {
  if (comb_l_[0].line.empty()) return;
  if (target_send_ <= 0.0 && send_ <= 1e-5 && !tail_) return;
  const double ease =
      1.0 - std::exp(-kEasePerSecond * frames / rate_);  // per block
  decay_ += (target_decay_ - decay_) * ease;
  UpdateFeedback();
  const double send_step = (target_send_ - send_) * ease / frames;

  float energy = 0;
  for (int f = 0; f < frames; ++f) {
    send_ += send_step;
    float* const frame = out + static_cast<ptrdiff_t>(f) * 2;
    const float* const source = send + static_cast<ptrdiff_t>(f) * 2;
    const float in = 0.5f * (source[0] + source[1]) * 0.25f;
    float wet[2] = {0, 0};
    for (int side = 0; side < 2; ++side) {
      auto& combs = side == 0 ? comb_l_ : comb_r_;
      auto& allpass = side == 0 ? all_l_ : all_r_;
      float sum = 0;
      for (size_t i = 0; i < combs.size(); ++i) {
        Comb& c = combs[i];
        const float y = c.line[c.pos];
        c.damp += (y - c.damp) * (1.0f - kDamping);
        c.line[c.pos] = in + c.damp * feedback_[i];
        c.pos = (c.pos + 1) % c.line.size();
        sum += y;
      }
      for (AllPass& a : allpass) {
        const float buffered = a.line[a.pos];
        const float input = sum;
        sum = buffered - kAllPassGain * input;
        a.line[a.pos] = input + kAllPassGain * buffered;
        a.pos = (a.pos + 1) % a.line.size();
      }
      wet[side] = sum;
    }
    energy = std::max(energy, std::max(std::fabs(wet[0]), std::fabs(wet[1])));
    const auto level = static_cast<float>(send_);
    frame[0] += wet[0] * level;
    frame[1] += wet[1] * level;
  }
  // While the tail is audible keep running even if the send was set to 0.
  tail_ = energy > 1e-5f;
}
}
