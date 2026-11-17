#pragma once
// The sounds of a game as data: which clips play for each sound event, the ambience loop of
// each zone, and how voice-over is played. Loaded from a sounds.json:
//   "events":   { "<Event>": { "clips": ["a", "b"], "gain": 0.8, "pitch": 1.0, "jitter": true }, ... }
//   "ambience": { "<zone>": { "clip": "house", "gain": 0.3 }, ... }
//   "voice":    { "dir": "voices", "gain": 1.0, "duck": 0.45, "radioFalloff": 0.1 }
// ("_" keys are comments; pitch and jitter are optional, everything else is required.)
#include <map>
#include <string>
#include <vector>

#include "engine/audio/audio_math.h"
#include "engine/core/json.h"

namespace mythos {

struct SoundBank {
    std::map<std::string, SoundCue> events;    // by event name
    std::map<std::string, SoundCue> ambience;  // by zone
    // Voice-over: <voiceDir>/<lang>/<text key>.wav.
    std::string voiceDir;
    float voiceGain = 1.0f;
    float voiceDuck = 1.0f;     // ambience level while a line is spoken
    float radioFalloff = 0.1f;  // how fast a radio's voice fades with distance (see positionalGain)

    // Every clip name any event or ambience uses, once each.
    std::vector<std::string> allClips() const;
};

// Loads `root` into `out`. The events and ambience zones the game needs must exist (others in
// the file load too). Returns false and says why in *error otherwise.
bool loadSoundBank(const json::Value& root, const std::string& file, SoundBank& out,
                   const std::vector<std::string>& requiredEvents, const std::vector<std::string>& requiredAmbience,
                   std::string* error);

}  // namespace mythos
