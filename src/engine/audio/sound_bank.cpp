#include "engine/audio/sound_bank.h"

#include <set>

#include "engine/core/json_reader.h"

namespace mythos {

std::vector<std::string> SoundBank::allClips() const {
    std::set<std::string> seen;
    std::vector<std::string> out;
    for (const auto* group : {&events, &ambience})
        for (const auto& [name, cue] : *group)
            for (const auto& clip : cue.clips)
                if (seen.insert(clip).second) out.push_back(clip);
    return out;
}

bool loadSoundBank(const json::Value& root, const std::string& file, SoundBank& bank,
                   const std::vector<std::string>& requiredEvents, const std::vector<std::string>& requiredAmbience,
                   std::string* error) {
    json::Reader r(root, file);
    // Reads one cue: an event lists several clips ("clips"), an ambience names one ("clip").
    auto readCue = [&](const std::string& path, bool severalClips) {
        SoundCue cue;
        if (severalClips) {
            const json::Value* clips = r.at(path + ".clips");
            if (clips && clips->is_array())
                for (const auto& clip : *clips)
                    if (clip.is_string()) cue.clips.push_back(clip.get<std::string>());
            if (clips && cue.clips.empty()) r.fail("\"" + path + ".clips\" must list at least one clip");
        } else {
            cue.clips.push_back(r.string(path + ".clip"));
        }
        cue.gain = static_cast<float>(r.number(path + ".gain"));
        if (const json::Value* definition = r.at(path)) {  // optional fields
            if (const json::Value* pitch = json::find(*definition, "pitch"); pitch && pitch->is_number())
                cue.pitch = static_cast<float>(pitch->get<double>());
            if (const json::Value* jitter = json::find(*definition, "jitter"); jitter && jitter->is_boolean())
                cue.jitter = jitter->get<bool>();
        }
        return cue;
    };
    for (const std::string& event : requiredEvents) bank.events[event] = readCue("events." + event, true);
    // Every event and ambience in the file loads (a game's data may name more than it requires).
    if (const json::Value* all = r.at("events"); all && all->is_object())
        for (const auto& [name, definition] : all->items())
            if (!name.empty() && name[0] != '_' && !bank.events.count(name)) bank.events[name] = readCue("events." + name, true);
    if (const json::Value* all = r.at("ambience"); all && all->is_object())
        for (const auto& [name, definition] : all->items())
            if (!name.empty() && name[0] != '_') bank.ambience[name] = readCue("ambience." + name, false);
    for (const std::string& zone : requiredAmbience)
        if (!bank.ambience.count(zone)) r.fail("missing \"ambience." + zone + "\"");
    bank.voiceDir = r.string("voice.dir");
    bank.voiceGain = static_cast<float>(r.number("voice.gain"));
    bank.voiceDuck = static_cast<float>(r.number("voice.duck"));
    bank.radioFalloff = static_cast<float>(r.number("voice.radioFalloff"));
    if (r.ok() && bank.radioFalloff <= 0) r.fail("voice.radioFalloff must be positive");
    if (!r.ok() && error) *error = r.error();
    return r.ok();
}

}  // namespace mythos
