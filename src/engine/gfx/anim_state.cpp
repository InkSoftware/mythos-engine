#include "engine/gfx/anim_state.h"

#include <algorithm>
#include <utility>

namespace mythos {
bool AnimStateMachine::AddState(const std::string& name,
                                const std::string& clip) {
  if (name.empty() || std::ranges::any_of(states_, [&](const StateDef& s) {
        return s.name == name;
      })) {
    return false;
  }
  const auto it = std::ranges::find_if(
      *clips_, [&](const Clip& c) { return c.name == clip; });
  if (it == clips_->end()) return false;
  states_.push_back({name, &*it});
  if (states_.size() == 1) Enter(name, 0);
  return true;
}

bool AnimStateMachine::Holds(const AnimCondition& c) const {
  const auto it = params_.find(c.param);
  const double v = it == params_.end() ? 0 : it->second;
  switch (c.op) {
    case AnimOp::kGreater:
      return v > c.value;
    case AnimOp::kLess:
      return v < c.value;
    case AnimOp::kEqual:
      return v == c.value;
    case AnimOp::kNotEqual:
      return v != c.value;
  }
  return false;
}

void AnimStateMachine::Enter(const std::string& state, const double fade) {
  const auto it = std::ranges::find_if(
      states_, [&](const StateDef& s) { return s.name == state; });
  if (it == states_.end()) return;
  state_ = state;
  animator_.Play(it->clip, fade);
}

void AnimStateMachine::Update(const double dt) {
  for (const AnimTransition& t : transitions_) {
    if (t.from != "*" && t.from != state_) continue;
    if (t.to == state_) continue;
    if (t.when_finished && !animator_.Finished()) continue;
    if (!std::ranges::all_of(
            t.conditions, [&](const AnimCondition& c) { return Holds(c); })) {
      continue;
    }
    Enter(t.to, t.fade);
    break;
  }
  animator_.Update(dt);
}
}
