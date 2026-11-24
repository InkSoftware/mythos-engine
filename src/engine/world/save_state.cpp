#include "engine/world/save_state.h"

#include <algorithm>
#include <string>
#include <vector>

namespace mythos {
namespace {
void ReadStrings(const json::Value& v, const char* key,
                 std::vector<std::string>& out) {
  const json::Value* list = json::Find(v, key);
  if (list == nullptr || !list->is_array()) return;
  for (const auto& item : *list) {
    if (item.is_string()) Progress::AddUnique(out, item.get<std::string>());
  }
}

double ReadNumber(const json::Value& v, const char* key, const double dflt) {
  const json::Value* n = json::Find(v, key);
  return n != nullptr && n->is_number() ? n->get<double>() : dflt;
}
}

json::Value ProgressToJson(const Progress& progress) {
  json::Value v = json::Value::object();
  v["clues"] = progress.clues;
  v["notebook"] = progress.notebook;
  v["held"] = progress.held;
  json::Value flags = json::Value::object();
  for (const auto& [name, value] : progress.flags) flags[name] = value;
  v["flags"] = std::move(flags);
  return v;
}

bool ProgressFromJson(const json::Value& v, Progress& out) {
  if (!v.is_object()) return false;
  Progress p;
  ReadStrings(v, "clues", p.clues);
  ReadStrings(v, "notebook", p.notebook);
  if (const json::Value* held = json::Find(v, "held");
      held != nullptr && held->is_string()) {
    p.held = held->get<std::string>();
  }
  if (const json::Value* flags = json::Find(v, "flags");
      flags != nullptr && flags->is_object()) {
    for (const auto& [name, value] : flags->items()) {
      if (value.is_number_integer()) p.flags[name] = value.get<int>();
    }
  }
  out = std::move(p);
  return true;
}

json::Value LevelDiffToJson(const Level& baseline, const Level& now) {
  json::Value out = json::Value::array();
  const int w = std::min(baseline.Width(), now.Width());
  const int h = std::min(baseline.Height(), now.Height());
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (baseline.At(x, y) != now.At(x, y)) {
        out.push_back({x, y, std::string(1, now.At(x, y))});
      }
    }
  }
  return out;
}

int ApplyLevelDiff(Level& level, const json::Value& diff) {
  if (!diff.is_array()) return 0;
  int applied = 0;
  for (const auto& entry : diff) {
    if (!entry.is_array() || entry.size() != 3 ||
        !entry[0].is_number_integer() || !entry[1].is_number_integer() ||
        !entry[2].is_string() || entry[2].get<std::string>().size() != 1) {
      continue;
    }
    const int x = entry[0].get<int>(), y = entry[1].get<int>();
    if (!level.InBounds(x, y)) continue;
    level.Set(x, y, entry[2].get<std::string>()[0]);
    ++applied;
  }
  return applied;
}

namespace {
bool SamePlacement(const Placement& a, const Placement& b) {
  return a.x == b.x && a.y == b.y && a.model == b.model &&
         a.yaw_deg == b.yaw_deg && a.off_x == b.off_x && a.off_z == b.off_z &&
         a.lift == b.lift && a.solid == b.solid && a.id == b.id;
}

bool HasPlacement(const Level& level, const Placement& p) {
  return std::ranges::any_of(level.Furniture(), [&](const Placement& q) {
    return SamePlacement(p, q);
  });
}
}  // namespace

json::Value FurnitureDiffToJson(const Level& baseline, const Level& now) {
  json::Value removed = json::Value::array();
  json::Value added = json::Value::array();
  for (const Placement& p : baseline.Furniture()) {
    if (!p.id.empty() && !HasPlacement(now, p)) removed.push_back(p.id);
  }
  for (const Placement& p : now.Furniture()) {
    if (HasPlacement(baseline, p)) continue;
    json::Value v = json::Value::object();
    v["x"] = p.x;
    v["y"] = p.y;
    v["model"] = p.model;
    v["yaw"] = p.yaw_deg;
    v["off_x"] = p.off_x;
    v["off_z"] = p.off_z;
    v["lift"] = p.lift;
    v["solid"] = p.solid;
    v["id"] = p.id;
    added.push_back(v);
  }
  json::Value out = json::Value::object();
  out["removed"] = removed;
  out["added"] = added;
  return out;
}

int ApplyFurnitureDiff(Level& level, const json::Value& diff) {
  if (!diff.is_object()) return 0;
  int changes = 0;
  if (diff.contains("removed") && diff["removed"].is_array()) {
    for (const auto& id : diff["removed"]) {
      if (id.is_string() && level.RemovePlacement(id.get<std::string>())) {
        ++changes;
      }
    }
  }
  if (diff.contains("added") && diff["added"].is_array()) {
    for (const auto& v : diff["added"]) {
      if (!v.is_object()) continue;
      Placement p;
      p.x = static_cast<int>(ReadNumber(v, "x", 0));
      p.y = static_cast<int>(ReadNumber(v, "y", 0));
      p.yaw_deg = ReadNumber(v, "yaw", 0);
      p.off_x = ReadNumber(v, "off_x", 0);
      p.off_z = ReadNumber(v, "off_z", 0);
      p.lift = ReadNumber(v, "lift", 0);
      if (!v.contains("model") || !v["model"].is_string()) continue;
      p.model = v["model"].get<std::string>();
      if (v.contains("solid") && v["solid"].is_boolean()) {
        p.solid = v["solid"].get<bool>();
      }
      if (v.contains("id") && v["id"].is_string()) {
        p.id = v["id"].get<std::string>();
      }
      if (level.AddFurniture(p)) ++changes;
    }
  }
  return changes;
}

json::Value PlayerToJson(const Player& player) {
  json::Value v = json::Value::object();
  v["x"] = player.pos.x;
  v["z"] = player.pos.y;
  v["angle"] = player.angle;
  v["pitch"] = player.pitch;
  v["flashlight"] = player.flashlight;
  return v;
}

bool PlayerFromJson(const json::Value& v, Player& out) {
  if (!v.is_object()) return false;
  Player p;
  p.pos.x = ReadNumber(v, "x", 0);
  p.pos.y = ReadNumber(v, "z", 0);
  p.angle = ReadNumber(v, "angle", 0);
  p.pitch = ReadNumber(v, "pitch", 0);
  const json::Value* light = json::Find(v, "flashlight");
  p.flashlight = light != nullptr && light->is_boolean() && light->get<bool>();
  out = p;
  return true;
}
}
