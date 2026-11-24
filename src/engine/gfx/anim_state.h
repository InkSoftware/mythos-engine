// A small animation state machine on top of Animator: named states that each
// play a clip, parameters the game sets (a bool or a number) and transitions
// that fire on a condition, or when a non-looping clip has finished, with a
// crossfade. Headless.
#ifndef MYTHOS_ENGINE_GFX_ANIM_STATE_H_
#define MYTHOS_ENGINE_GFX_ANIM_STATE_H_

#include <map>
#include <string>
#include <vector>

#include "engine/gfx/skeleton.h"

namespace mythos {
enum class AnimOp { kGreater, kLess, kEqual, kNotEqual };

// `param op value`; a bool parameter is 1 (true) or 0 (false). A parameter
// never set reads 0.
struct AnimCondition {
  std::string param;
  AnimOp op = AnimOp::kEqual;
  double value = 1;
};

struct AnimTransition {
  // The state it leaves, or "*" for any.
  std::string from;
  std::string to;
  // All must hold (none: always, which suits `when_finished`).
  std::vector<AnimCondition> conditions;
  // Also requires the current (non-looping) clip to have finished.
  bool when_finished = false;
  double fade = 0.2;
};

class AnimStateMachine {
 public:
  // `skeleton` and `clips` must outlive the machine (clips are found by name).
  AnimStateMachine(const Skeleton* skeleton, const std::vector<Clip>* clips)
      : animator_(skeleton), clips_(clips) {}

  // Adds a state playing the clip called `clip`. The first state added is the
  // one the machine starts in. Returns false for a clip that does not exist or
  // a name already used.
  bool AddState(const std::string& name, const std::string& clip);
  // Transitions are tried in the order added.
  void AddTransition(AnimTransition transition) {
    transitions_.push_back(std::move(transition));
  }

  void SetBool(const std::string& param, bool value) {
    params_[param] = value ? 1 : 0;
  }
  void SetFloat(const std::string& param, double value) {
    params_[param] = value;
  }

  // Takes at most one transition, then advances the clip.
  void Update(double dt);

  [[nodiscard]] const std::string& State() const { return state_; }
  [[nodiscard]] Pose CurrentPose() const { return animator_.CurrentPose(); }

 private:
  [[nodiscard]] bool Holds(const AnimCondition& c) const;
  void Enter(const std::string& state, double fade);

  struct StateDef {
    std::string name;
    const Clip* clip = nullptr;
  };
  Animator animator_;
  const std::vector<Clip>* clips_;
  std::vector<StateDef> states_;
  std::vector<AnimTransition> transitions_;
  std::map<std::string, double> params_;
  std::string state_;
};
}

#endif
