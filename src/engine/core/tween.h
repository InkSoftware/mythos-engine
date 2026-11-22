#ifndef MYTHOS_ENGINE_CORE_TWEEN_H_
#define MYTHOS_ENGINE_CORE_TWEEN_H_

#include <cstddef>
#include <functional>
#include <vector>

#include "engine/core/math.h"

// Easing curves, tweens that run on a game's own clock (`Update(dt)`, no
// timers inside) and a sequence of waits, tweens and calls. Pure logic.
namespace mythos {
// Easing curves: each maps 0..1 to 0..1 (kOutBack and kOutElastic overshoot 1
// on the way). "In" starts slow, "Out" ends slow.
enum class Ease : int {
  kLinear,
  kSmoothStep,
  kInQuad,
  kOutQuad,
  kInOutQuad,
  kInCubic,
  kOutCubic,
  kInOutCubic,
  kInOutSine,
  kOutBack,
  kOutElastic,
  kOutBounce,
};

inline constexpr int kEaseCount = 12;

// `t` is clamped to 0..1 first; `t` = 0 gives 0 and `t` = 1 gives 1 exactly.
double ApplyEase(Ease ease, double t);

// Linear blend of two values, `t` unclamped (an easing may overshoot).
inline double Mix(const double a, const double b, const double t) {
  return a + (b - a) * t;
}

inline float Mix(const float a, const float b, const double t) {
  return static_cast<float>(a + (b - a) * t);
}

inline Vec2 Mix(const Vec2& a, const Vec2& b, const double t) {
  return {Mix(a.x, b.x, t), Mix(a.y, b.y, t)};
}

inline Vec3 Mix(const Vec3& a, const Vec3& b, const double t) {
  return {Mix(a.x, b.x, t), Mix(a.y, b.y, t), Mix(a.z, b.z, t)};
}

// A value moving from `from` to `to` over `duration` seconds after `delay`
// seconds. T is float, double, Vec2 or Vec3 (anything `Mix` takes). A duration
// of 0 or less jumps to `to` once the delay is over.
template <typename T>
class Tween {
 public:
  Tween() = default;

  Tween(const T& from, const T& to, const double duration,
        const Ease ease = Ease::kLinear, const double delay = 0)
      : from_(from),
        to_(to),
        duration_(duration),
        delay_(delay > 0 ? delay : 0),
        ease_(ease) {}

  // Moves the clock on; a negative `dt` is ignored.
  void Update(const double dt) {
    if (dt > 0) elapsed_ += dt;
  }

  // Where the value is now: `from` during the delay, `to` when done.
  [[nodiscard]] T Value() const { return Mix(from_, to_, Eased()); }

  // How far on the clock the tween is, 0..1 (not eased).
  [[nodiscard]] double Progress() const {
    if (duration_ <= 0) return elapsed_ >= delay_ ? 1.0 : 0.0;
    return Clampd((elapsed_ - delay_) / duration_, 0.0, 1.0);
  }

  [[nodiscard]] bool Done() const { return Progress() >= 1.0; }

  // Back to the start, same end points.
  void Reset() { elapsed_ = 0; }

  // Starts again from where the value is now, towards `to`, over `duration`
  // seconds (so a target can change mid-flight without a jump).
  void Retarget(const T& to, const double duration) {
    from_ = Value();
    to_ = to;
    duration_ = duration;
    delay_ = 0;
    elapsed_ = 0;
  }

 private:
  [[nodiscard]] double Eased() const { return ApplyEase(ease_, Progress()); }

  T from_{};
  T to_{};
  double duration_ = 0;
  double delay_ = 0;
  double elapsed_ = 0;
  Ease ease_ = Ease::kLinear;
};

// Steps that run one after the other on the game's clock: wait, tween a number
// into a callback, call a function. Time left over when a step ends carries on
// into the next one, so a long frame skips nothing. Callbacks may call
// `Reset()` but must not add steps.
class Sequence {
 public:
  // Does nothing for `seconds`.
  Sequence& Wait(double seconds);

  // Calls `apply` with the value on every `Update` while the step runs: first
  // with `from`, last with `to` exactly.
  Sequence& Tween(double from, double to, double seconds, Ease ease,
                  std::function<void(double)> apply);

  // Calls `fn` once, when the sequence reaches it.
  Sequence& Call(std::function<void()> fn);

  // Moves the clock on by `dt` seconds (negative is ignored).
  void Update(double dt);

  // True when every step has run (an empty sequence is done).
  [[nodiscard]] bool Done() const { return index_ >= steps_.size(); }

  // Runs from the first step again.
  void Reset();

  // Drops every step.
  void Clear();

  [[nodiscard]] size_t StepCount() const { return steps_.size(); }

 private:
  struct Step {
    double duration = 0;
    double from = 0, to = 0;
    Ease ease = Ease::kLinear;
    std::function<void(double)> apply;
    std::function<void()> call;
  };

  std::vector<Step> steps_;
  size_t index_ = 0;
  double elapsed_ = 0;
  bool begun_ = false;
};
}  // namespace mythos

#endif
