#include "engine/audio/sound_bank.h"

#include <set>

#include "engine/core/json_reader.h"

namespace mythos {

std::vector<std::string> SoundBank::AllClips() const {
  std::set<std::string> seen;
  std::vector<std::string> out;
  for (const auto* group : {&events, &ambience}) {
    for (const auto& [name, cue] : *group) {
      for (const auto& clip : cue.clips) {
        if (seen.insert(clip).second) out.push_back(clip);
      }
    }
  }
  return out;
}

bool LoadSoundBank(const json::Value& root, const std::string& file,
                   SoundBank& bank,
                   const std::vector<std::string>& required_events,
                   const std::vector<std::string>& required_ambience,
                   std::string* error) {
  json::Reader r(root, file);
  // Reads one cue: an event lists several clips ("clips"), an ambience names
  // one ("clip").
  const auto read_cue = [&](const std::string& path, bool several_clips) {
    SoundCue cue;
    if (several_clips) {
      const json::Value* clips = r.At(path + ".clips");
      if (clips && clips->is_array()) {
        for (const auto& clip : *clips) {
          if (clip.is_string()) cue.clips.push_back(clip.get<std::string>());
        }
      }
      if (clips && cue.clips.empty()) {
        r.Fail("\"" + path + ".clips\" must list at least one clip");
      }
    } else {
      cue.clips.push_back(r.String(path + ".clip"));
    }
    cue.gain = static_cast<float>(r.Number(path + ".gain"));
    if (const json::Value* definition = r.At(path)) {  // optional fields
      if (const json::Value* pitch = json::Find(*definition, "pitch");
          pitch && pitch->is_number()) {
        cue.pitch = static_cast<float>(pitch->get<double>());
      }
      if (const json::Value* jitter = json::Find(*definition, "jitter");
          jitter && jitter->is_boolean()) {
        cue.jitter = jitter->get<bool>();
      }
    }
    return cue;
  };
  for (const std::string& event : required_events) {
    bank.events[event] = read_cue("events." + event, true);
  }
  // Every event and ambience in the file loads (a game's data may name more
  // than it requires).
  if (const json::Value* all = r.At("events"); all && all->is_object()) {
    for (const auto& [name, definition] : all->items()) {
      if (!name.empty() && name[0] != '_' && !bank.events.contains(name)) {
        bank.events[name] = read_cue("events." + name, true);
      }
    }
  }
  if (const json::Value* all = r.At("ambience"); all && all->is_object()) {
    for (const auto& [name, definition] : all->items()) {
      if (!name.empty() && name[0] != '_') {
        bank.ambience[name] = read_cue("ambience." + name, false);
      }
    }
  }
  for (const std::string& zone : required_ambience) {
    if (!bank.ambience.contains(zone)) {
      r.Fail("missing \"ambience." + zone + "\"");
    }
  }
  bank.voice_dir = r.String("voice.dir");
  bank.voice_gain = static_cast<float>(r.Number("voice.gain"));
  bank.voice_duck = static_cast<float>(r.Number("voice.duck"));
  bank.radio_falloff = static_cast<float>(r.Number("voice.radioFalloff"));
  if (r.Ok() && bank.radio_falloff <= 0) {
    r.Fail("voice.radioFalloff must be positive");
  }
  if (!r.Ok() && error) *error = r.Error();
  return r.Ok();
}

}  // namespace mythos
