#pragma once
// The achievements: counters of things that happened, and which counters have crossed their
// goal. They belong to the player, not to a run, so they live in their own profile file and
// survive new games. Plain data with a JSON round trip, so it is tested headless.

#include <map>
#include <string>
#include <vector>

#include "engine/core/json.h"

namespace mythos {

// data/achievements.json: unlocked when the counter of `event` reaches `count`.
struct AchievementDef {
    std::string id;
    std::string event;
    int count = 1;
    // Translation keys: achievements.<id>.name and achievements.<id>.desc.
    std::string name, desc;
};

// Reads an achievements file: { "achievements": { "<id>": { "event": ..., "count": ... }, ... } }
// ("_" keys are comments). The texts are the keys achievements.<id>.name and .desc.
bool loadAchievementDefs(const json::Value& root, const std::string& file, std::vector<AchievementDef>& out,
                         std::string* error);

class Achievements {
public:
    void setDefs(const std::vector<AchievementDef>* defs) { defs_ = defs; }

    // Counts an event: adds `value` to its counter, or (absolute) raises the counter to at
    // least `value`. Returns the ids of the achievements this unlocked, in file order.
    std::vector<std::string> record(const std::string& event, int value = 1, bool absolute = false);

    bool unlocked(const std::string& id) const;
    int unlockedCount() const { return static_cast<int>(unlocked_.size()); }
    int stat(const std::string& event) const;

    std::string toJson() const;
    // Replaces the progress with the profile in text. On failure leaves it alone and says why.
    bool fromJson(const std::string& text, std::string* error = nullptr);

private:
    const std::vector<AchievementDef>* defs_ = nullptr;
    std::map<std::string, int> stats_;
    std::vector<std::string> unlocked_;
};

}  // namespace mythos
