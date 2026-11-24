#include "engine/ui/input_map.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mythos {
namespace {
struct KindName {
  BindKind kind;
  const char* name;
};
constexpr KindName kKinds[] = {{BindKind::kKey, "key"},
                               {BindKind::kPadButton, "pad"},
                               {BindKind::kPadAxis, "axis"},
                               {BindKind::kMouseButton, "mouse"}};
}

std::optional<Binding> ParseBinding(const std::string& text) {
  const size_t colon = text.find(':');
  if (colon == std::string::npos) return std::nullopt;
  const std::string head = text.substr(0, colon);
  std::string code = text.substr(colon + 1);
  for (const auto& k : kKinds) {
    if (head != k.name) continue;
    Binding b;
    b.kind = k.kind;
    if (k.kind == BindKind::kPadAxis) {
      if (code.size() < 2) return std::nullopt;
      const char s = code.back();
      if (s != '+' && s != '-') return std::nullopt;
      b.sign = s == '+' ? 1 : -1;
      code.pop_back();
    }
    if (code.empty()) return std::nullopt;
    b.code = std::move(code);
    return b;
  }
  return std::nullopt;
}

std::string BindingToText(const Binding& b) {
  std::string out;
  for (const auto& k : kKinds) {
    if (k.kind == b.kind) out = k.name;
  }
  out += ':' + b.code;
  if (b.kind == BindKind::kPadAxis) out += b.sign < 0 ? '-' : '+';
  return out;
}

double ApplyDeadZone(const double value, double dead) {
  dead = std::clamp(dead, 0.0, 0.95);
  const double mag = std::fabs(value);
  if (mag <= dead) return 0.0;
  const double scaled = std::min(1.0, (mag - dead) / (1.0 - dead));
  return value < 0 ? -scaled : scaled;
}

double AxisStrength(const Binding& b, const double raw_value,
                    const double dead) {
  const double v = ApplyDeadZone(raw_value, dead);
  return v * b.sign > 0 ? std::fabs(v) : 0.0;
}

InputMap::Action* InputMap::Find(const std::string& name) {
  for (auto& a : actions_) {
    if (a.name == name) return &a;
  }
  return nullptr;
}

const InputMap::Action* InputMap::Find(const std::string& name) const {
  for (const auto& a : actions_) {
    if (a.name == name) return &a;
  }
  return nullptr;
}

void InputMap::Define(const std::string& action, const std::string& context,
                      std::vector<Binding> defaults) {
  if (defaults.size() > kSlots) defaults.resize(kSlots);
  if (Action* a = Find(action)) {
    a->context = context;
    a->defaults = defaults;
    a->current = std::move(defaults);
    return;
  }
  actions_.push_back({action, context, defaults, defaults});
}

bool InputMap::Has(const std::string& action) const {
  return Find(action) != nullptr;
}

std::vector<std::string> InputMap::Actions(const std::string& context) const {
  std::vector<std::string> out;
  for (const auto& a : actions_) {
    if (a.context == context) out.push_back(a.name);
  }
  return out;
}

const std::vector<Binding>& InputMap::Bindings(
    const std::string& action) const {
  static const std::vector<Binding> kNone;
  const Action* a = Find(action);
  return a != nullptr ? a->current : kNone;
}

std::string InputMap::Resolve(const std::string& context,
                              const Binding& b) const {
  for (const auto& a : actions_) {
    if (a.context != context) continue;
    if (std::ranges::find(a.current, b) != a.current.end()) {
      return a.name;
    }
  }
  return {};
}

std::string InputMap::FindConflict(const std::string& action,
                                   const Binding& b) const {
  const Action* self = Find(action);
  if (self == nullptr) return {};
  for (const auto& a : actions_) {
    if (a.context != self->context || a.name == action) continue;
    if (std::ranges::find(a.current, b) != a.current.end()) {
      return a.name;
    }
  }
  return {};
}

std::string InputMap::Rebind(const std::string& action, const size_t slot,
                             const Binding& b, const bool steal) {
  Action* a = Find(action);
  if (a == nullptr || slot >= kSlots) return {};
  std::string other = FindConflict(action, b);
  if (!other.empty()) {
    if (!steal) return other;
    auto& list = Find(other)->current;
    std::erase(list, b);
  }
  // The same input twice on one action is a wasted slot.
  std::erase(a->current, b);
  if (a->current.size() < slot) a->current.resize(slot);
  if (a->current.size() == slot) {
    a->current.push_back(b);
  } else {
    a->current[slot] = b;
  }
  return {};
}

void InputMap::Unbind(const std::string& action, const size_t slot) {
  Action* a = Find(action);
  if (a != nullptr && slot < a->current.size()) {
    a->current.erase(a->current.begin() + static_cast<std::ptrdiff_t>(slot));
  }
}

void InputMap::Reset(const std::string& action) {
  if (Action* a = Find(action)) a->current = a->defaults;
}

void InputMap::ResetAll() {
  for (auto& a : actions_) a.current = a.defaults;
}

json::Value InputMap::ToJson() const {
  json::Value v = json::Value::object();
  for (const auto& a : actions_) {
    if (a.current == a.defaults) continue;
    json::Value list = json::Value::array();
    for (const auto& b : a.current) list.push_back(BindingToText(b));
    v[a.name] = std::move(list);
  }
  return v;
}

bool InputMap::FromJson(const json::Value& v) {
  if (!v.is_object()) return false;
  ResetAll();
  // Cleared first so overrides can swap inputs between actions.
  std::vector<std::pair<Action*, std::vector<Binding>>> wanted;
  for (const auto& [name, list] : v.items()) {
    Action* a = Find(name);
    if (a == nullptr || !list.is_array()) continue;
    std::vector<Binding> bindings;
    for (const auto& item : list) {
      if (!item.is_string()) continue;
      const auto b = ParseBinding(item.get<std::string>());
      if (b && bindings.size() < kSlots &&
          std::ranges::find(bindings, *b) == bindings.end()) {
        bindings.push_back(*b);
      }
    }
    wanted.emplace_back(a, std::move(bindings));
  }
  for (auto& [a, bindings] : wanted) a->current.clear();
  for (auto& [a, bindings] : wanted) {
    for (const auto& b : bindings) {
      if (FindConflict(a->name, b).empty()) a->current.push_back(b);
    }
  }
  return true;
}

void ActionTracker::Update(const InputMap& map, const std::string& context,
                           const BindingProbe& probe) {
  before_ = std::move(now_);
  now_.clear();
  for (const std::string& action : map.Actions(context)) {
    double strength = 0;
    for (const Binding& b : map.Bindings(action)) {
      strength = std::max(strength, std::clamp(probe(b), 0.0, 1.0));
    }
    now_[action] = strength;
  }
}

double ActionTracker::Strength(const std::string& action) const {
  const auto it = now_.find(action);
  return it == now_.end() ? 0.0 : it->second;
}

bool ActionTracker::Pressed(const std::string& action) const {
  const auto it = before_.find(action);
  return Down(action) && (it == before_.end() || it->second <= 0);
}

bool ActionTracker::Released(const std::string& action) const {
  const auto it = before_.find(action);
  return !Down(action) && it != before_.end() && it->second > 0;
}

InputMap DefaultGameplayMap() {
  const auto key = [](const char* name) {
    return Binding{BindKind::kKey, name, 0};
  };
  InputMap map;
  map.Define("forward", kPlayContext, {key("W"), key("Up")});
  map.Define("back", kPlayContext, {key("S"), key("Down")});
  map.Define("strafe_left", kPlayContext, {key("A")});
  map.Define("strafe_right", kPlayContext, {key("D")});
  map.Define("turn_left", kPlayContext, {key("Left")});
  map.Define("turn_right", kPlayContext, {key("Right")});
  map.Define("run", kPlayContext, {key("Left Shift"), key("Right Shift")});
  map.Define("interact", kPlayContext, {key("E"), key("Space")});
  return map;
}
}
