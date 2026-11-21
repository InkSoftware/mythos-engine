#include "engine/world/interact.h"

#include <algorithm>
#include <cmath>

namespace mythos {
bool ParseVerb(const std::string& word, Verb& out) {
  if (word == "read") {
    out = Verb::kRead;
  } else if (word == "inspect") {
    out = Verb::kInspect;
  } else if (word == "listen") {
    out = Verb::kListen;
  } else if (word == "take") {
    out = Verb::kTake;
  } else {
    return false;
  }
  return true;
}

void PropOverride::ApplyTo(Prop& prop) const {
  if (model) prop.model = *model;
  if (loop) prop.loop_clip = *loop;
  if (name) prop.name = *name;
  if (text) prop.text = *text;
  if (notebook) prop.notebook = *notebook;
  if (at) prop.at = *at;
  if (floor) prop.floor = *floor;
  if (lift) prop.lift = *lift;
  if (yaw_deg) prop.yaw_deg = *yaw_deg;
  if (aim_height) prop.aim_height = *aim_height;
  if (aim_radius) prop.aim_radius = *aim_radius;
  if (seconds) prop.seconds = *seconds;
  if (verb) prop.verb = *verb;
}

std::vector<std::string> PropOverride::TextKeys() const {
  std::vector<std::string> out;
  for (const auto* key : {&text, &name, &notebook}) {
    if (*key) out.push_back(**key);
  }
  return out;
}

Placement PlacementOf(const Prop& prop) {
  Placement out;
  out.x = static_cast<int>(std::floor(prop.at.x));
  out.y = static_cast<int>(std::floor(prop.at.y));
  out.off_x = prop.at.x - (out.x + 0.5);
  out.off_z = prop.at.y - (out.y + 0.5);
  out.lift = prop.lift;
  out.yaw_deg = prop.yaw_deg;
  out.model = prop.model;
  out.solid = false;
  out.id = prop.id;
  return out;
}

bool Progress::AddUnique(std::vector<std::string>& list,
                         const std::string& key) {
  if (Contains(list, key)) return false;
  list.push_back(key);
  return true;
}

bool Progress::Contains(const std::vector<std::string>& list,
                        const std::string& key) {
  return std::ranges::find(list, key) != list.end();
}

bool ApplyEffects(Progress& progress, const Effects& effects) {
  if (!effects.notebook.empty()) progress.AddNote(effects.notebook);
  const bool new_clue = !effects.clue.empty() &&
                        Progress::AddUnique(progress.clues, effects.clue);
  for (const auto& [flag, value] : effects.flags) progress.flags[flag] = value;
  return new_clue;
}

Use Interactor::Use(const Prop& prop, const bool home, const std::string& held,
                    const double dt, const bool pressed, const bool holding,
                    const double listen_seconds) {
  // The empty place of the prop in hand: the only thing to do is put it back.
  if (home) {
    listen_ = 0;
    return pressed ? Use::kPutBack : Use::kNone;
  }
  if (prop.verb != Verb::kListen) listen_ = 0;
  switch (prop.verb) {
    case Verb::kRead:
      return pressed ? Use::kRead : Use::kNone;
    case Verb::kInspect:
      return pressed ? Use::kInspect : Use::kNone;
    case Verb::kListen:
      if (!holding || listen_done_) {
        listen_ = 0;
        return Use::kNone;
      }
      listen_ += dt;
      if (listen_ < listen_seconds) return Use::kListening;
      listen_ = 0;
      listen_done_ = true;
      return Use::kHeard;
    case Verb::kTake:
      if (!pressed) return Use::kNone;
      return held.empty() ? Use::kTake : Use::kHandsFull;
  }
  return Use::kNone;
}
}
