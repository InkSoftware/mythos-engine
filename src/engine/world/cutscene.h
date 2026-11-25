// A cutscene: a timeline of camera keys, timed cues (events for the game to
// carry out), named value tracks (a fade, an actor's position) and animation
// cues, played by a `CutscenePlayer` that the game steps each frame. Headless:
// it builds on the tweens' easings and the event actions of the rules, and
// the game decides what a cue or a value means. Files are text (see
// ParseCutscene) so the editor and a game can both read and write them.
#ifndef MYTHOS_ENGINE_WORLD_CUTSCENE_H_
#define MYTHOS_ENGINE_WORLD_CUTSCENE_H_

#include <functional>
#include <string>
#include <vector>

#include "engine/core/cvar.h"
#include "engine/core/events.h"
#include "engine/core/math.h"
#include "engine/core/tween.h"

namespace mythos {
// The camera at a time. `ease` shapes the move from the key before into this
// one. `pos` is (x, eye height, z) in world units; yaw and pitch in radians
// (the player's convention); `fov_scale` as CameraState::fov_scale.
struct CameraKey {
  double time = 0;
  Vec3 pos;
  double yaw = 0, pitch = 0, fov_scale = 1;
  Ease ease = Ease::kSmoothStep;

  bool operator==(const CameraKey&) const = default;
};

struct CameraSample {
  Vec3 pos;
  double yaw = 0, pitch = 0, fov_scale = 1;
};

// An action for the game to carry out at a time (the rules' EventAction: a
// type and an argument, such as "message" and a translation key).
struct CueKey {
  double time = 0;
  EventAction action;

  bool operator==(const CueKey&) const = default;
};

struct ValueKey {
  double time = 0;
  double value = 0;
  Ease ease = Ease::kLinear;

  bool operator==(const ValueKey&) const = default;
};

// A number over time under a name: "fade", "guide.x", whatever the game reads.
struct ValueTrack {
  std::string name;
  std::vector<ValueKey> keys;

  bool operator==(const ValueTrack&) const = default;
};

// Start a clip on an actor (the game maps the actor name to a character).
struct AnimKey {
  double time = 0;
  std::string actor, clip;
  double fade = 0.2;

  bool operator==(const AnimKey&) const = default;
};

struct Cutscene {
  std::string name;
  // Seconds; 0 means the time of the last key.
  double duration = 0;
  std::vector<CameraKey> camera;
  std::vector<CueKey> cues;
  std::vector<ValueTrack> values;
  std::vector<AnimKey> anims;

  bool operator==(const Cutscene&) const = default;
};

// The length: `duration`, or the last key when that is longer or unset.
double CutsceneLength(const Cutscene& scene);

// Easing names as written in files ("linear", "smoothstep", "in_quad", ...).
std::string EaseName(Ease ease);
bool ParseEase(const std::string& name, Ease* out);

// The text format. `;` starts a comment line. A `duration <seconds>` line, then
// sections:
//   [camera]  time x y z yaw pitch fov_scale ease
//   [cues]    time type [argument, to the end of the line]
//   [values]  name time value ease
//   [anims]   time actor clip fade
// Keys may come in any order (they are sorted by time). Returns false with the
// reason in `error`; `out` is untouched then.
bool ParseCutscene(const std::string& text, Cutscene* out, std::string* error);
std::string SerializeCutscene(const Cutscene& scene);

// Puts every list of keys in time order (stable: keys at one time keep their
// order). The player and the file writer expect it; an editor calls it after
// the times were changed.
void SortCutscene(Cutscene& scene);

// A key of a scene, for a timeline that picks and drags them. `row` is
// `kRowCamera`, `kRowCue`, `kRowAnim` or `kRowValue`; `index` is the key in
// its list, and for a value key `track` is the value track.
enum : int { kRowCamera = 0, kRowCue = 1, kRowAnim = 2, kRowValue = 3 };
struct KeyRef {
  int row = 0;
  size_t track = 0;
  size_t index = 0;

  bool operator==(const KeyRef&) const = default;
};

// The key of row `row` nearest to `time` and within `tolerance` seconds, or
// false. Of two at the same distance the first wins.
bool FindCutsceneKey(const Cutscene& scene, int row, double time,
                     double tolerance, KeyRef* out);

// The time of a key; 0 for a reference that does not exist.
double CutsceneKeyTime(const Cutscene& scene, const KeyRef& key);

// Moves a key to `time` (never below 0). The list is not re-sorted: a drag
// keeps its index until `SortCutscene`. Returns false for a reference that
// does not exist.
bool MoveCutsceneKey(Cutscene& scene, const KeyRef& key, double time);

// What is wrong with a cutscene, one line each (empty when nothing is): a
// negative or non-finite time, a cue with no type, an animation key with no
// actor or clip, a value track with no name or a duplicate name, a camera key
// with a non-finite number. A cutscene with no keys at all and no duration is
// reported too (it ends at once and shows nothing).
std::vector<std::string> ValidateCutscene(const Cutscene& scene);

// The camera at time `t` of a cutscene without playing it (an editor scrubbing
// the timeline); false when it has no camera keys. Cues and animation keys
// are not fired.
bool SampleCutsceneCamera(const Cutscene& scene, double t, CameraSample* out);
// A value track at time `t`, or `fallback` when there is none by that name.
double SampleCutsceneValue(const Cutscene& scene, const std::string& name,
                           double t, double fallback = 0);

// What the console's cutscene commands do, supplied by the game.
struct CutsceneHooks {
  // Starts the cutscene called `name`; false (with a reason) when there is no
  // such one.
  std::function<bool(const std::string& name, std::string* error)> start;
  std::function<void()> skip;
  std::function<void()> stop;
  // The names of the cutscenes there are.
  std::function<std::vector<std::string>()> list;
};

// Registers `cutscene.play <name>`, `cutscene.skip`, `cutscene.stop` and
// `cutscene.list` on a cvar registry (the debug console), so a cutscene can
// be run and tested by hand. A missing hook makes its command report that.
void RegisterCutsceneCommands(CvarRegistry& reg, const CutsceneHooks& hooks);

using CueCallback = std::function<void(const EventAction&)>;
using AnimCallback = std::function<void(const AnimKey&)>;

// Plays a cutscene (not owned: keep it alive while playing).
class CutscenePlayer {
 public:
  void Start(const Cutscene* scene);
  void Stop() { scene_ = nullptr; }

  [[nodiscard]] bool Playing() const { return scene_ != nullptr && !done_; }
  [[nodiscard]] bool Finished() const { return scene_ != nullptr && done_; }
  [[nodiscard]] double Time() const { return time_; }

  // Advances by `dt` and fires the cues and animation keys passed, in time
  // order (a key at the start fires on the first update). At the end the
  // player is Finished and holds the last camera and values.
  void Update(double dt, const CueCallback& on_cue,
              const AnimCallback& on_anim);

  // Jumps to the end, firing every key not yet fired (a skipped cutscene must
  // still leave its flags set).
  void Skip(const CueCallback& on_cue, const AnimCallback& on_anim);

  // The camera now; false before a cutscene without camera keys.
  [[nodiscard]] bool Camera(CameraSample* out) const;
  // A value track now, or `fallback` when there is none by that name.
  [[nodiscard]] double Value(const std::string& name,
                             double fallback = 0) const;

 private:
  const Cutscene* scene_ = nullptr;
  std::vector<CueKey> cues_;
  std::vector<AnimKey> anims_;
  size_t next_cue_ = 0, next_anim_ = 0;
  double time_ = 0, length_ = 0;
  bool done_ = false;
  bool started_ = false;
};
}

#endif
