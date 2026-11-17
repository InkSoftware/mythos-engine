#include "engine/audio/sound_bank.h"

#include <set>

#include "engine/core/json_reader.h"

namespace mythos {

std::vector<std::string> SoundBank::allClips() const {
    std::set<std::string> seen;
    std::vector<std::string> out;
    for (const auto* group : {&events, &ambience})
        for (const auto& [name, cue] : *group)
            for (const auto& c : cue.clips)
                if (seen.insert(c).second) out.push_back(c);
    return out;
}

bool loadSoundBank(const json::Value& root, const std::string& file, SoundBank& s,
                   const std::vector<std::string>& requiredEvents, const std::vector<std::string>& requiredAmbience,
                   std::string* error) {
    json::Reader r(root, file);
    auto cue = [&](const std::string& path, bool many) {
        SoundCue c;
        if (many) {
            const json::Value* clips = r.at(path + ".clips");
            if (clips && clips->is_array())
                for (const auto& v : *clips)
                    if (v.is_string()) c.clips.push_back(v.get<std::string>());
            if (clips && c.clips.empty()) r.fail("\"" + path + ".clips\" must list at least one clip");
        } else {
            c.clips.push_back(r.string(path + ".clip"));
        }
        c.gain = static_cast<float>(r.number(path + ".gain"));
        if (const json::Value* def = r.at(path)) {  // optional fields
            if (const json::Value* p = json::find(*def, "pitch"); p && p->is_number()) c.pitch = static_cast<float>(p->get<double>());
            if (const json::Value* j = json::find(*def, "jitter"); j && j->is_boolean()) c.jitter = j->get<bool>();
        }
        return c;
    };
    for (const std::string& e : requiredEvents) s.events[e] = cue("events." + e, true);
    // Every event and ambience in the file loads (a game's data may name more than it requires).
    if (const json::Value* all = r.at("events"); all && all->is_object())
        for (const auto& [name, v] : all->items())
            if (!name.empty() && name[0] != '_' && !s.events.count(name)) s.events[name] = cue("events." + name, true);
    if (const json::Value* all = r.at("ambience"); all && all->is_object())
        for (const auto& [name, v] : all->items())
            if (!name.empty() && name[0] != '_') s.ambience[name] = cue("ambience." + name, false);
    for (const std::string& z : requiredAmbience)
        if (!s.ambience.count(z)) r.fail("missing \"ambience." + z + "\"");
    s.voiceDir = r.string("voice.dir");
    s.voiceGain = static_cast<float>(r.number("voice.gain"));
    s.voiceDuck = static_cast<float>(r.number("voice.duck"));
    s.radioFalloff = static_cast<float>(r.number("voice.radioFalloff"));
    if (r.ok() && s.radioFalloff <= 0) r.fail("voice.radioFalloff must be positive");
    if (!r.ok() && error) *error = r.error();
    return r.ok();
}

}  // namespace mythos
