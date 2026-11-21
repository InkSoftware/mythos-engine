// The data side of a game's sound: which clips play for which event or ambience
// zone, and the voice-over settings. Loaded from JSON; the platform mixer plays
// it.
#ifndef MYTHOS_ENGINE_AUDIO_SOUND_BANK_H_
#define MYTHOS_ENGINE_AUDIO_SOUND_BANK_H_

#include <map>
#include <string>
#include <vector>

#include "engine/audio/audio_math.h"
#include "engine/core/json.h"

namespace mythos {
// All cues of a game.
struct SoundBank {
  // One-shot effects keyed by event name.
  std::map<std::string, SoundCue> events;
  // Looping beds keyed by zone name; each has a single clip.
  std::map<std::string, SoundCue> ambience;

  // Directory of voice-over clips.
  std::string voice_dir;
  // Linear volume of voice lines.
  float voice_gain = 1.0f;
  // Gain applied to other sound while a voice line plays.
  float voice_duck = 1.0f;
  // Distance falloff of radio voices; positive.
  float radio_falloff = 0.1f;

  // Every distinct clip name in events and ambience, in first-seen order, for
  // preloading.
  [[nodiscard]] std::vector<std::string> AllClips() const;
};

// Fills `out` from the parsed `root` (`file` names it in messages). Every name
// in `required_events` and `required_ambience` must exist; other events and
// ambience entries are read too, and names starting with an underscore are
// skipped as comments. Returns false on the first problem and stores its
// message in `*error` when given.
bool LoadSoundBank(const json::Value& root, const std::string& file,
                   SoundBank& out,
                   const std::vector<std::string>& required_events,
                   const std::vector<std::string>& required_ambience,
                   std::string* error);
}

#endif
