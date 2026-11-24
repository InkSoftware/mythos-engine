#include "engine/world/save_state.h"

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
