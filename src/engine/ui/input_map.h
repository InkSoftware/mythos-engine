// Named input actions bound to keys, buttons and axes, with contexts (an
// action conflicts only inside its own), rebinding and persistence. Headless:
// a binding names its input as text ("key:W", "pad:South", "axis:LeftX-",
// "mouse:Left"); the platform layer turns SDL events into the same names.
#ifndef MYTHOS_ENGINE_UI_INPUT_MAP_H_
#define MYTHOS_ENGINE_UI_INPUT_MAP_H_

#include <optional>
#include <string>
#include <vector>

#include "engine/core/json.h"

namespace mythos {
enum class BindKind { kKey, kPadButton, kPadAxis, kMouseButton };

struct Binding {
  BindKind kind = BindKind::kKey;
  // The input's name: "W", "South", "LeftX", "Left".
  std::string code;
  // Axes only: -1 or +1, the half of the axis that counts.
  int sign = 0;

  bool operator==(const Binding& o) const {
    return kind == o.kind && code == o.code && sign == o.sign;
  }
};

// "key:W", "pad:South", "axis:LeftX+", "mouse:Left"; nullopt if malformed.
std::optional<Binding> ParseBinding(const std::string& text);
std::string BindingToText(const Binding& b);

// A stick value with its dead zone removed and the rest stretched to 0..1,
// keeping the sign. `dead` is clamped to 0..0.95.
double ApplyDeadZone(double value, double dead);

// The strength (0..1) with which `b` is down given the raw axis value, or
// 0 for the wrong half of the axis.
double AxisStrength(const Binding& b, double raw_value, double dead);

class InputMap {
 public:
  static constexpr size_t kSlots = 2;

  // Declares an action with its default bindings (at most kSlots used).
  void Define(const std::string& action, const std::string& context,
              std::vector<Binding> defaults);

  [[nodiscard]] bool Has(const std::string& action) const;
  // Action names of a context, in definition order.
  [[nodiscard]] std::vector<std::string> Actions(
      const std::string& context) const;
  [[nodiscard]] const std::vector<Binding>& Bindings(
      const std::string& action) const;

  // The action of `context` that `b` triggers, or "".
  [[nodiscard]] std::string Resolve(const std::string& context,
                                    const Binding& b) const;

  // Another action of the same context already using `b`, or "".
  [[nodiscard]] std::string FindConflict(const std::string& action,
                                         const Binding& b) const;

  // Sets slot `slot` of `action` to `b`. A conflict is refused (returns the
  // other action's name and changes nothing) unless `steal`, which unbinds
  // it from the other action. Returns "" on success.
  std::string Rebind(const std::string& action, size_t slot, const Binding& b,
                     bool steal = false);
  void Unbind(const std::string& action, size_t slot);
  void Reset(const std::string& action);
  void ResetAll();

  // Only what differs from the defaults is saved.
  [[nodiscard]] json::Value ToJson() const;
  // Unknown actions, malformed bindings and conflicts are skipped, so an
  // old file never breaks a newer game. Returns false if `v` is not an object.
  bool FromJson(const json::Value& v);

 private:
  struct Action {
    std::string name, context;
    std::vector<Binding> defaults, current;
  };
  Action* Find(const std::string& name);
  [[nodiscard]] const Action* Find(const std::string& name) const;
  std::vector<Action> actions_;
};
}

#endif
