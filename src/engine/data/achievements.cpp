#include "engine/data/achievements.h"

#include <algorithm>
#include <cmath>

#include "engine/core/json.h"
#include "engine/core/json_reader.h"

namespace mythos {

bool LoadAchievementDefs(const json::Value& root, const std::string& file,
                         std::vector<AchievementDef>& out, std::string* error) {
  json::Reader reader(root, file);
  const json::Value* all = reader.At("achievements");
  if (all && !all->is_object()) {
    reader.Fail("\"achievements\" must be an object");
  }
  if (all && all->is_object()) {
    for (const auto& [id, definition] : all->items()) {
      if (!id.empty() && id[0] == '_') continue;
      const std::string path = "achievements." + id;
      AchievementDef def;
      def.id = id;
      def.event = reader.String(path + ".event");
      def.count = reader.Integer(path + ".count");
      def.name = path + ".name";  // translation keys, not the text
      def.desc = path + ".desc";
      if (reader.Ok() && (def.event.empty() || def.count < 1)) {
        reader.Fail("\"" + path + "\" needs an event and a count of 1 or more");
      }
      out.push_back(def);
    }
  }
  if (!reader.Ok() && error) *error = reader.Error();
  return reader.Ok();
}

int Achievements::Stat(const std::string& event) const {
  const auto it = stats_.find(event);
  return it == stats_.end() ? 0 : it->second;
}

bool Achievements::Unlocked(const std::string& id) const {
  return std::ranges::find(unlocked_, id) != unlocked_.end();
}

std::vector<std::string> Achievements::Record(const std::string& event,
                                              int value, bool absolute) {
  int& total = stats_[event];
  total = absolute ? std::max(total, value) : total + value;
  std::vector<std::string> gained;
  if (!defs_) return gained;
  for (const AchievementDef& def : *defs_) {
    if (def.event == event && total >= def.count && !Unlocked(def.id)) {
      unlocked_.push_back(def.id);
      gained.push_back(def.id);
    }
  }
  return gained;
}

std::string Achievements::ToJson() const {
  std::string out = "{\n  \"unlocked\": [";
  for (size_t i = 0; i < unlocked_.size(); ++i) {
    out += (i ? ", " : "") + json::Quote(unlocked_[i]);
  }
  out += "],\n  \"stats\": {";
  bool first = true;
  for (const auto& [event, count] : stats_) {
    out +=
        (first ? "" : ", ") + json::Quote(event) + ": " + std::to_string(count);
    first = false;
  }
  return out + "}\n}\n";
}

bool Achievements::FromJson(const std::string& text, std::string* error) {
  const auto fail = [&](const std::string& what) {
    if (error) *error = "profile file: " + what;
    return false;
  };
  json::Value root;
  std::string parse_error;
  if (!json::Parse(text, root, &parse_error)) return fail(parse_error);
  const json::Value* list = json::Find(root, "unlocked");
  const json::Value* stats = json::Find(root, "stats");
  if (!list || !list->is_array() || !stats || !stats->is_object()) {
    return fail(R"(needs "unlocked" and "stats")");
  }
  std::vector<std::string> unlocked;
  std::map<std::string, int> counters;
  for (const auto& entry : *list) {
    if (!entry.is_string()) return fail("\"unlocked\" must hold strings");
    unlocked.push_back(entry.get<std::string>());
  }
  for (const auto& [event, count] : stats->items()) {
    if (!count.is_number() ||
        count.get<double>() != std::floor(count.get<double>())) {
      return fail("stat \"" + event + "\" must be a whole number");
    }
    counters[event] = static_cast<int>(count.get<double>());
  }
  unlocked_ = std::move(unlocked);
  stats_ = std::move(counters);
  return true;
}

}  // namespace mythos
