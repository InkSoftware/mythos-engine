#include "engine/core/tween.h"

#include <cmath>
#include <numbers>
#include <utility>

namespace mythos {
namespace {
double OutBounce(double t) {
  constexpr double n = 7.5625, d = 2.75;
  if (t < 1 / d) return n * t * t;
  if (t < 2 / d) {
    t -= 1.5 / d;
    return n * t * t + 0.75;
  }
  if (t < 2.5 / d) {
    t -= 2.25 / d;
    return n * t * t + 0.9375;
  }
  t -= 2.625 / d;
  return n * t * t + 0.984375;
}
}  // namespace

double ApplyEase(const Ease ease, double t) {
  if (!(t > 0)) return 0;  // also catches NaN
  if (t >= 1) return 1;
  switch (ease) {
    case Ease::kLinear:
      return t;
    case Ease::kSmoothStep:
      return t * t * (3 - 2 * t);
    case Ease::kInQuad:
      return t * t;
    case Ease::kOutQuad:
      return 1 - (1 - t) * (1 - t);
    case Ease::kInOutQuad:
      return t < 0.5 ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2) / 2;
    case Ease::kInCubic:
      return t * t * t;
    case Ease::kOutCubic:
      return 1 - std::pow(1 - t, 3);
    case Ease::kInOutCubic:
      return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
    case Ease::kInOutSine:
      return -(std::cos(std::numbers::pi * t) - 1) / 2;
    case Ease::kOutBack: {
      constexpr double c1 = 1.70158, c3 = c1 + 1;
      return 1 + c3 * std::pow(t - 1, 3) + c1 * std::pow(t - 1, 2);
    }
    case Ease::kOutElastic: {
      constexpr double c4 = 2 * std::numbers::pi / 3;
      return std::pow(2, -10 * t) * std::sin((t * 10 - 0.75) * c4) + 1;
    }
    case Ease::kOutBounce:
      return OutBounce(t);
  }
  return t;
}

Sequence& Sequence::Wait(const double seconds) {
  Step s;
  s.duration = seconds > 0 ? seconds : 0;
  steps_.push_back(std::move(s));
  return *this;
}

Sequence& Sequence::Tween(const double from, const double to,
                          const double seconds, const Ease ease,
                          std::function<void(double)> apply) {
  Step s;
  s.duration = seconds > 0 ? seconds : 0;
  s.from = from;
  s.to = to;
  s.ease = ease;
  s.apply = std::move(apply);
  steps_.push_back(std::move(s));
  return *this;
}

Sequence& Sequence::Call(std::function<void()> fn) {
  Step s;
  s.call = std::move(fn);
  steps_.push_back(std::move(s));
  return *this;
}

void Sequence::Update(const double dt) {
  double left = dt > 0 ? dt : 0;
  while (index_ < steps_.size()) {
    const size_t at = index_;
    const double duration = steps_[at].duration;
    if (steps_[at].call) {
      // Move out first: the callback may Reset() and run this step again.
      const std::function<void()> fn = steps_[at].call;
      ++index_;
      elapsed_ = 0;
      begun_ = false;
      fn();
      continue;
    }
    if (steps_[at].apply && !begun_) {
      begun_ = true;
      const std::function<void(double)> fn = steps_[at].apply;
      fn(steps_[at].from);
      if (index_ != at) continue;  // the callback reset the sequence
    }
    begun_ = true;
    const double need = duration - elapsed_;
    if (left < need) {
      elapsed_ += left;
      if (steps_[at].apply && duration > 0) {
        const std::function<void(double)> fn = steps_[at].apply;
        fn(Mix(steps_[at].from, steps_[at].to,
               ApplyEase(steps_[at].ease, elapsed_ / duration)));
      }
      return;
    }
    left -= need;
    const std::function<void(double)> fn = steps_[at].apply;
    const double to = steps_[at].to;
    ++index_;
    elapsed_ = 0;
    begun_ = false;
    if (fn) fn(to);
  }
}

void Sequence::Reset() {
  index_ = 0;
  elapsed_ = 0;
  begun_ = false;
}

void Sequence::Clear() {
  steps_.clear();
  Reset();
}
}  // namespace mythos
