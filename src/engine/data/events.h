#ifndef MYTHOS_ENGINE_DATA_EVENTS_H_
#define MYTHOS_ENGINE_DATA_EVENTS_H_

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mythos {
enum class EventKind { kTrigger, kFlag, kTimer };

// For triggers, detail identifies the hit (for example, "enter" or "use").
// For flags, value is the new value. Timers are identified by id alone.
// Callers translate world trigger hits and supply flag and timer events; this
// runtime does not detect changes or advance clocks.
struct Event {
  EventKind kind = EventKind::kTrigger;
  std::string id;
  std::string detail;
  bool value = false;
};

struct EventCondition {
  std::string flag;
  bool value = true;
};

// Action types and arguments are interpreted by the game. For example, a
// game may register handlers for "set_flag", "play_sound", or its own types.
struct EventAction {
  std::string type;
  std::string argument;
};

struct EventRule {
  std::string id;
  Event event;
  std::vector<EventCondition> conditions;
  std::vector<EventAction> actions;
};

inline bool ValidEvent(const Event& event) {
  if (event.id.empty()) return false;
  switch (event.kind) {
    case EventKind::kTrigger:
      return !event.detail.empty();
    case EventKind::kFlag:
    case EventKind::kTimer:
      return true;
    default:
      return false;
  }
}

inline bool ValidEventRule(const EventRule& rule) {
  if (rule.id.empty() || !ValidEvent(rule.event)) return false;
  for (const auto& condition : rule.conditions)
    if (condition.flag.empty()) return false;
  for (const auto& action : rule.actions)
    if (action.type.empty()) return false;
  return true;
}

// Rules are evaluated in ascending ID order, with actions in their listed
// order. Missing flags are false; all conditions must match. Event fields not
// relevant to their kind are ignored. Rules with no actions are valid.
class EventRuntime {
 public:
  using Flags = std::map<std::string, bool>;
  using Callback = std::function<void(const EventAction&)>;

  // Invalid rules and duplicate IDs are rejected without changing the rules.
  bool Add(const EventRule& rule) {
    if (!ValidEventRule(rule)) return false;
    return rules_.emplace(rule.id, rule).second;
  }

  bool Remove(const std::string& id) { return rules_.erase(id) != 0; }
  void Clear() { rules_.clear(); }

  // Rejects invalid events or an empty callback without dispatching anything.
  // Matching and action copying complete before any callback. Changes made by
  // callbacks, including nested dispatches, affect only subsequent evaluations;
  // they cannot change the actions already selected by this call. Exceptions
  // from the callback propagate and stop the remaining dispatches.
  bool Dispatch(const Event& event, const Flags& flags,
                const Callback& callback) const {
    if (!ValidEvent(event) || !callback) return false;
    std::vector<EventAction> pending;
    for (const auto& [id, rule] : rules_) {
      if (rule.event.kind != event.kind || rule.event.id != event.id) continue;
      if (event.kind == EventKind::kTrigger &&
          rule.event.detail != event.detail)
        continue;
      if (event.kind == EventKind::kFlag && rule.event.value != event.value)
        continue;
      bool matches = true;
      for (const auto& condition : rule.conditions) {
        const auto flag = flags.find(condition.flag);
        const bool value = flag != flags.end() && flag->second;
        if (value != condition.value) {
          matches = false;
          break;
        }
      }
      if (matches)
        pending.insert(pending.end(), rule.actions.begin(), rule.actions.end());
    }
    // The supplied callback itself may be changed by an invocation.
    const Callback dispatch = callback;
    for (const auto& action : pending) dispatch(action);
    return true;
  }

 private:
  std::map<std::string, EventRule> rules_;
};
}

#endif