// Achievement definitions (data) and per-player progress (stats and unlocks).
#ifndef MYTHOS_ENGINE_DATA_ACHIEVEMENTS_H_
#define MYTHOS_ENGINE_DATA_ACHIEVEMENTS_H_

#include <map>
#include <string>
#include <vector>

#include "engine/core/json.h"

namespace mythos {
// One achievement: unlocked once the stat `event` reaches `count`.
struct AchievementDef {
  // Unique key, also stored in the save.
  std::string id;
  std::string event;
  int count = 1;

  // Translation keys for the title and description, not the text.
  std::string name, desc;
};

// Appends the definitions in `root["achievements"]` to `out`; keys starting
// with an underscore are comments. Returns false on bad data, with a message in
// `*error`.
bool LoadAchievementDefs(const json::Value& root, const std::string& file,
                         std::vector<AchievementDef>& out, std::string* error);

// Tracks event counters and the unlocks they trigger. Does not own the
// definitions.
class Achievements {
 public:
  // `defs` must outlive this object; null disables unlocking.
  void SetDefs(const std::vector<AchievementDef>* const defs) { defs_ = defs; }

  // Adds `value` to the counter `event`, or with `absolute` raises it to at
  // least `value`. Returns the ids unlocked by this call.
  std::vector<std::string> Record(const std::string& event, int value = 1,
                                  bool absolute = false);

  [[nodiscard]] bool Unlocked(const std::string& id) const;

  [[nodiscard]] int UnlockedCount() const {
    return static_cast<int>(unlocked_.size());
  }

  [[nodiscard]] int Stat(const std::string& event) const;

  // Serializes the profile (unlocks and counters) as JSON.
  [[nodiscard]] std::string ToJson() const;

  // Replaces the profile from ToJson output. On failure returns false and
  // leaves the current state untouched.
  bool FromJson(const std::string& text, std::string* error = nullptr);

 private:
  const std::vector<AchievementDef>* defs_ = nullptr;
  std::map<std::string, int> stats_;
  std::vector<std::string> unlocked_;
};
}

#endif
