#ifndef MYTHOS_ENGINE_WORLD_TRIGGER_H_
#define MYTHOS_ENGINE_WORLD_TRIGGER_H_

#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "engine/core/math.h"

namespace mythos {
enum class TriggerEvent { kEnter, kExit, kStay, kUse };
enum class TriggerShape { kBox, kCell };

// A trigger emits only its selected event. Boxes use inclusive world-space
// bounds; cells occupy [x, x+1) by [z, z+1) on the specified floor, at any
// height. Only the fields for the selected shape are used.
struct Trigger {
  std::string id;
  TriggerShape shape = TriggerShape::kBox;
  TriggerEvent event = TriggerEvent::kEnter;
  bool once = false;
  Vec3 min, max;
  int cell_x = 0, cell_z = 0, floor = 0;

  bool operator==(const Trigger& other) const {
    return id == other.id && shape == other.shape && event == other.event &&
           once == other.once && min.x == other.min.x && min.y == other.min.y &&
           min.z == other.min.z && max.x == other.max.x &&
           max.y == other.max.y && max.z == other.max.z &&
           cell_x == other.cell_x && cell_z == other.cell_z &&
           floor == other.floor;
  }
};

struct TriggerHit {
  std::string id;
  TriggerEvent event;
};

// Invalid enum values, empty IDs, non-finite or reversed box bounds are
// rejected.
inline bool ValidTrigger(const Trigger& trigger) {
  if (trigger.id.empty()) return false;
  switch (trigger.event) {
    case TriggerEvent::kEnter:
    case TriggerEvent::kExit:
    case TriggerEvent::kStay:
    case TriggerEvent::kUse:
      break;
    default:
      return false;
  }
  switch (trigger.shape) {
    case TriggerShape::kBox:
      return std::isfinite(trigger.min.x) && std::isfinite(trigger.min.y) &&
             std::isfinite(trigger.min.z) && std::isfinite(trigger.max.x) &&
             std::isfinite(trigger.max.y) && std::isfinite(trigger.max.z) &&
             trigger.min.x <= trigger.max.x && trigger.min.y <= trigger.max.y &&
             trigger.min.z <= trigger.max.z;
    case TriggerShape::kCell:
      return true;
    default:
      return false;
  }
}

// [triggers] lines: id box event once minX minY minZ maxX maxY maxZ,
// or id cell event once cellX cellZ floor. Event is enter, exit, stay or use;
// once is 0 or 1. IDs are nonempty single tokens without control characters,
// not beginning with '[' or ';'.
// Parsing leaves out untouched on failure; serialization returns empty if
// invalid.
bool ParseTriggerLine(const std::string& line, Trigger* out);
std::string SerializeTriggerLine(const Trigger& trigger);

// Tracks one point subject. Use a separate runtime per subject. Sampling is
// discrete: crossing a volume entirely between updates is not detected.
class TriggerRuntime {
 public:
  // Rejects invalid triggers and duplicate IDs without changing the runtime.
  bool Add(const Trigger& trigger) {
    if (!ValidTrigger(trigger)) return false;
    return triggers_.emplace(trigger.id, State{trigger}).second;
  }

  // Removal and reset are silent: neither synthesizes exit events.
  bool Remove(const std::string& id) { return triggers_.erase(id) != 0; }
  void Clear() { triggers_.clear(); }
  // Re-arms once triggers and treats the subject as outside on the next update.
  void Reset() {
    for (auto& [id, state] : triggers_) {
      state.inside = false;
      state.fired = false;
    }
  }

  // Replaces hits in ascending ID order. Enter and exit are edges; stay fires
  // only on subsequent inside samples, and use fires on each supplied press
  // edge while inside (including on entry). Once applies to the selected event
  // for the lifetime of the trigger until Reset. Initial state is outside.
  // A non-finite position returns false and leaves both state and hits intact.
  bool Update(const Vec3& position, const int floor, const bool use_pressed,
              std::vector<TriggerHit>& hits) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z))
      return false;
    hits.clear();
    for (auto& [id, state] : triggers_) {
      const Trigger& t = state.trigger;
      const bool inside =
          t.shape == TriggerShape::kBox
              ? position.x >= t.min.x && position.x <= t.max.x &&
                    position.y >= t.min.y && position.y <= t.max.y &&
                    position.z >= t.min.z && position.z <= t.max.z
              : floor == t.floor &&
                    position.x >= static_cast<double>(t.cell_x) &&
                    position.x < static_cast<double>(t.cell_x) + 1.0 &&
                    position.z >= static_cast<double>(t.cell_z) &&
                    position.z < static_cast<double>(t.cell_z) + 1.0;
      bool fire = false;
      switch (t.event) {
        case TriggerEvent::kEnter:
          fire = inside && !state.inside;
          break;
        case TriggerEvent::kExit:
          fire = !inside && state.inside;
          break;
        case TriggerEvent::kStay:
          fire = inside && state.inside;
          break;
        case TriggerEvent::kUse:
          fire = inside && use_pressed;
          break;
      }
      state.inside = inside;
      if (fire && !(t.once && state.fired)) {
        hits.push_back({id, t.event});
        state.fired = true;
      }
    }
    return true;
  }

 private:
  struct State {
    Trigger trigger;
    bool inside = false;
    bool fired = false;
  };
  std::map<std::string, State> triggers_;
};
}

#endif