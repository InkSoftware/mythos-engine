#ifndef MYTHOS_ENGINE_WORLD_INTERACT_H_
#define MYTHOS_ENGINE_WORLD_INTERACT_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/math.h"
#include "engine/world/level.h"

// Using things in a first-person world: what a prop is, what the player can do
// to it, and what using it leaves behind. Nothing here knows a story; a game
// decides what each outcome means.
namespace mythos {
// What a prop does when used. Read opens a document; inspect shows a line;
// listen needs the key held for a while, then shows a line; take moves it into
// the hand (using its empty place puts it back).
enum class Verb { kRead, kInspect, kListen, kTake };

// Parses "read", "inspect", "listen" or "take"; false for any other word.
bool ParseVerb(const std::string& word, Verb& out);

// An object the player can use. Its texts are translation keys, resolved by the
// game through Tr().
struct Prop {
  std::string id;
  // Model name; empty draws nothing (an empty place you can still look at).
  std::string model;
  // World x, z.
  Vec2 at;
  // Story of the building: 0 ground, 1 above, -1 cellar.
  int floor = 0;

  // Height of the base above the floor, and yaw in degrees (0 faces +z).
  double lift = 0, yaw_deg = 0;
  // Sphere the view ray must hit to aim at the prop.
  double aim_height = 0, aim_radius = 0;
  Verb verb = Verb::kInspect;
  // How long its line stays up (not used by kRead).
  double seconds = 0;
  // A looping sound it makes, placed in the world; empty for silence.
  std::string loop_clip;
  // The loop falls silent this close; 0 means never.
  double hush_radius = 0;
  // Translation keys for the title, the body text and the notebook entry.
  std::string name, text, notebook;

  [[nodiscard]] const std::string& NameKey() const { return name; }
  [[nodiscard]] const std::string& TextKey() const { return text; }
  [[nodiscard]] const std::string& NotebookKey() const { return notebook; }
};

// Optional replacements for fields of a Prop, so a game can vary a base prop
// per scenario; unset fields keep the base value.
struct PropOverride {
  std::optional<std::string> model, name, text, notebook, loop;
  std::optional<Vec2> at;
  std::optional<int> floor;
  std::optional<double> lift, yaw_deg, aim_height, aim_radius, seconds;
  std::optional<Verb> verb;

  // Writes each set field into p.
  void ApplyTo(Prop& p) const;

  // The translation keys this override sets (text, name, notebook), for
  // validation.
  [[nodiscard]] std::vector<std::string> TextKeys() const;
};

// The non-solid level Placement that draws a prop; the offset is measured from
// the center of the cell holding the prop.
Placement PlacementOf(const Prop& p);

// What the player has found. A game derives its own state from this.
struct Progress {
  // Clue keys, each at most once.
  std::vector<std::string> clues;
  // Notebook entry keys, each at most once.
  std::vector<std::string> notebook;
  // Id of the prop in the hand, empty if none.
  std::string held;
  // Named story flags; zero or absent counts as unset.
  std::map<std::string, int> flags;

  // Appends key if absent. Returns whether it was added.
  static bool AddUnique(std::vector<std::string>& list, const std::string& key);

  // Whether list holds key.
  static bool Contains(const std::vector<std::string>& list,
                       const std::string& key);

  bool AddNote(const std::string& key) { return AddUnique(notebook, key); }

  [[nodiscard]] bool Flag(const std::string& name) const {
    const auto it = flags.find(name);
    return it != flags.end() && it->second != 0;
  }
};

// What using something leaves behind: a sound to play (for the game to play), a
// notebook entry, a clue and flags to set.
struct Effects {
  std::string sound;
  std::string notebook;
  std::string clue;
  std::map<std::string, int> flags;
};

// Applies the notebook entry, clue and flags to progress. Returns true if the
// clue is new.
bool ApplyEffects(Progress& progress, const Effects& e);

// Outcome of one Interactor::Use call.
enum class Use {
  kNone,
  // Listening is in progress (the key is held, not yet long enough).
  kListening,
  kRead,
  kInspect,
  kHeard,
  kTake,

  // A take was refused because the hand already holds something.
  kHandsFull,
  kPutBack,
};

// Per-frame state of the interaction with the aimed prop: tracks how long a
// listen has been held.
class Interactor {
 public:
  // Call each frame with the key state; letting go re-arms listening.
  void Release(const bool interact_held) {
    if (!interact_held) listen_done_ = false;
  }

  // Call when nothing is aimed at: resets the listen timer.
  void Idle() { listen_ = 0; }

  // Resolves one frame of using prop. home is true when prop is the empty place
  // of the object in hand. dt is the frame time in seconds; pressed is the
  // press edge and holding the held state of the key; listen_seconds is how
  // long a kListen prop must be held. A listen completes once per press
  // (kHeard), then stays quiet until Release sees the key let go.
  mythos::Use Use(const Prop& p, bool home, const std::string& held, double dt,
                  bool pressed, bool holding, double listen_seconds);

  // Seconds of the current listen so far.
  [[nodiscard]] double ListenTime() const { return listen_; }

 private:
  double listen_ = 0;
  bool listen_done_ = false;
};
}

#endif
