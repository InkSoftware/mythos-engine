#ifndef MYTHOS_ENGINE_CORE_EVENTS_H_
#define MYTHOS_ENGINE_CORE_EVENTS_H_

#include <functional>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <string>
#include <utility>
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

  bool operator==(const Event&) const = default;
};

struct EventCondition {
  std::string flag;
  bool value = true;

  bool operator==(const EventCondition&) const = default;
};

// Action types and arguments are interpreted by the game. For example, a
// game may register handlers for "set_flag", "play_sound", or its own types.
struct EventAction {
  std::string type;
  std::string argument;

  bool operator==(const EventAction&) const = default;
};

struct EventRule {
  std::string id;
  Event event;
  std::vector<EventCondition> conditions;
  std::vector<EventAction> actions;

  bool operator==(const EventRule&) const = default;
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

namespace event_rule_detail {
inline std::string Encode(const std::string& value) {
  constexpr char kHex[] = "0123456789abcdef";
  std::string encoded = "x";
  for (const unsigned char c : value) {
    encoded += kHex[c >> 4];
    encoded += kHex[c & 15];
  }
  return encoded;
}

inline int HexDigit(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

inline bool ReadString(std::istream& in, std::string& value) {
  std::string token;
  if (!(in >> token) || token[0] != 'x' || token.size() % 2 != 1) return false;
  value.clear();
  for (size_t i = 1; i < token.size(); i += 2) {
    const int high = HexDigit(token[i]);
    const int low = HexDigit(token[i + 1]);
    if (high < 0 || low < 0) return false;
    value += static_cast<char>((high << 4) | low);
  }
  return true;
}

inline bool ReadBool(std::istream& in, bool& value) {
  std::string token;
  if (!(in >> token) || (token != "0" && token != "1")) return false;
  value = token == "1";
  return true;
}

inline bool ReadCount(std::istream& in, size_t& count) {
  std::string token;
  if (!(in >> token)) return false;
  count = 0;
  for (const char c : token) {
    if (c < '0' || c > '9') return false;
    const size_t digit = static_cast<size_t>(c - '0');
    if (count > (std::numeric_limits<size_t>::max() - digit) / 10) return false;
    count = count * 10 + digit;
  }
  return true;
}
}

// [rules] lines contain whitespace-separated tokens:
// rule1 id kind event_id detail event_value condition_count
//   (flag value)* action_count (type argument)*
// String tokens are 'x' followed by two hex digits per byte (just 'x' for an
// empty string). Booleans are 0 or 1, counts are unsigned decimal, and kind
// is trigger, flag or timer. All event fields are stored, even unused ones.
// Hex is case-insensitive on input; output uses lowercase. No line terminators
// or extra tokens are accepted. Rule IDs must be checked for duplicates by the
// section owner. Parsing leaves out untouched on failure (including null out);
// serialization returns empty if the rule fails ValidEventRule.
inline bool ParseEventRuleLine(const std::string& line, EventRule* const out) {
  if (!out || line.find_first_of("\r\n") != std::string::npos) return false;
  std::istringstream in(line);
  in.imbue(std::locale::classic());
  EventRule rule;
  std::string version, kind;
  if (!(in >> version) || version != "rule1" ||
      !event_rule_detail::ReadString(in, rule.id) || !(in >> kind))
    return false;
  if (kind == "trigger")
    rule.event.kind = EventKind::kTrigger;
  else if (kind == "flag")
    rule.event.kind = EventKind::kFlag;
  else if (kind == "timer")
    rule.event.kind = EventKind::kTimer;
  else
    return false;
  size_t count = 0;
  if (!event_rule_detail::ReadString(in, rule.event.id) ||
      !event_rule_detail::ReadString(in, rule.event.detail) ||
      !event_rule_detail::ReadBool(in, rule.event.value) ||
      !event_rule_detail::ReadCount(in, count) || count > line.size() / 2)
    return false;
  for (size_t i = 0; i < count; ++i) {
    EventCondition condition;
    if (!event_rule_detail::ReadString(in, condition.flag) ||
        !event_rule_detail::ReadBool(in, condition.value))
      return false;
    rule.conditions.push_back(std::move(condition));
  }
  if (!event_rule_detail::ReadCount(in, count) || count > line.size() / 2)
    return false;
  for (size_t i = 0; i < count; ++i) {
    EventAction action;
    if (!event_rule_detail::ReadString(in, action.type) ||
        !event_rule_detail::ReadString(in, action.argument))
      return false;
    rule.actions.push_back(std::move(action));
  }
  std::string extra;
  if ((in >> extra) || !ValidEventRule(rule)) return false;
  *out = std::move(rule);
  return true;
}

inline std::string SerializeEventRuleLine(const EventRule& rule) {
  if (!ValidEventRule(rule)) return {};
  const char* kind = nullptr;
  switch (rule.event.kind) {
    case EventKind::kTrigger:
      kind = "trigger";
      break;
    case EventKind::kFlag:
      kind = "flag";
      break;
    case EventKind::kTimer:
      kind = "timer";
      break;
    default:
      return {};
  }
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << "rule1 " << event_rule_detail::Encode(rule.id) << ' ' << kind << ' '
      << event_rule_detail::Encode(rule.event.id) << ' '
      << event_rule_detail::Encode(rule.event.detail) << ' '
      << (rule.event.value ? 1 : 0) << ' ' << rule.conditions.size();
  for (const auto& condition : rule.conditions)
    out << ' ' << event_rule_detail::Encode(condition.flag) << ' '
        << (condition.value ? 1 : 0);
  out << ' ' << rule.actions.size();
  for (const auto& action : rule.actions)
    out << ' ' << event_rule_detail::Encode(action.type) << ' '
        << event_rule_detail::Encode(action.argument);
  return out.str();
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