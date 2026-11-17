#include "engine/data/achievements.h"

#include <algorithm>
#include <cmath>

#include "engine/core/json.h"
#include "engine/core/json_reader.h"

namespace mythos {

bool loadAchievementDefs(const json::Value& root, const std::string& file, std::vector<AchievementDef>& out,
                         std::string* error) {
    json::Reader r(root, file);
    const json::Value* all = r.at("achievements");
    if (all && !all->is_object()) r.fail("\"achievements\" must be an object");
    if (all && all->is_object()) {
        for (const auto& [id, v] : all->items()) {
            if (!id.empty() && id[0] == '_') continue;
            const std::string at = "achievements." + id;
            AchievementDef a;
            a.id = id;
            a.event = r.string(at + ".event");
            a.count = r.integer(at + ".count");
            a.name = at + ".name";
            a.desc = at + ".desc";
            if (r.ok() && (a.event.empty() || a.count < 1))
                r.fail("\"" + at + "\" needs an event and a count of 1 or more");
            out.push_back(a);
        }
    }
    if (!r.ok() && error) *error = r.error();
    return r.ok();
}

int Achievements::stat(const std::string& event) const {
    const auto it = stats_.find(event);
    return it == stats_.end() ? 0 : it->second;
}

bool Achievements::unlocked(const std::string& id) const {
    return std::find(unlocked_.begin(), unlocked_.end(), id) != unlocked_.end();
}

std::vector<std::string> Achievements::record(const std::string& event, int value, bool absolute) {
    int& n = stats_[event];
    n = absolute ? std::max(n, value) : n + value;
    std::vector<std::string> gained;
    if (!defs_) return gained;
    for (const AchievementDef& d : *defs_)
        if (d.event == event && n >= d.count && !unlocked(d.id)) {
            unlocked_.push_back(d.id);
            gained.push_back(d.id);
        }
    return gained;
}

std::string Achievements::toJson() const {
    std::string out = "{\n  \"unlocked\": [";
    for (size_t i = 0; i < unlocked_.size(); ++i) out += (i ? ", " : "") + json::quote(unlocked_[i]);
    out += "],\n  \"stats\": {";
    bool first = true;
    for (const auto& [k, v] : stats_) {
        out += (first ? "" : ", ") + json::quote(k) + ": " + std::to_string(v);
        first = false;
    }
    return out + "}\n}\n";
}

bool Achievements::fromJson(const std::string& text, std::string* error) {
    auto fail = [&](const std::string& what) {
        if (error) *error = "profile file: " + what;
        return false;
    };
    json::Value root;
    std::string err;
    if (!json::parse(text, root, &err)) return fail(err);
    const json::Value* list = json::find(root, "unlocked");
    const json::Value* stats = json::find(root, "stats");
    if (!list || !list->is_array() || !stats || !stats->is_object()) return fail("needs \"unlocked\" and \"stats\"");
    std::vector<std::string> unlocked;
    std::map<std::string, int> counters;
    for (const auto& v : *list) {
        if (!v.is_string()) return fail("\"unlocked\" must hold strings");
        unlocked.push_back(v.get<std::string>());
    }
    for (const auto& [k, v] : stats->items()) {
        if (!v.is_number() || v.get<double>() != std::floor(v.get<double>())) return fail("stat \"" + k + "\" must be a whole number");
        counters[k] = static_cast<int>(v.get<double>());
    }
    unlocked_ = std::move(unlocked);
    stats_ = std::move(counters);
    return true;
}

}  // namespace mythos
