// Layered music: several loops that start together and stay in step, each
// with a gain that fades to a target, so a game brings a layer in (drums when
// something dangerous is near) or crossfades one set of layers to another (a
// new room's music). Headless: this is the fader; the mixer plays the loops
// (`AudioEngine::StartMusic`).
#ifndef MYTHOS_ENGINE_AUDIO_MUSIC_H_
#define MYTHOS_ENGINE_AUDIO_MUSIC_H_

#include <map>
#include <string>
#include <vector>

namespace mythos {
// The time of the next bar line at or after `now` for music at `bpm` beats a
// minute with `beats_per_bar` beats a bar (a bar line is a multiple of the
// bar's length from the start of the music). A bar line exactly at `now`
// counts as now.
double NextBarTime(double now, double bpm, int beats_per_bar);

// A stretch of music with its own tempo, from `start` seconds (the music's own
// clock) until the next section starts. Bars count from `start`.
struct TempoSection {
  double start = 0;
  double bpm = 120;
  int beats_per_bar = 4;
};

// The next bar line at or after `now` in a tempo map (sections in time order;
// a bar that would cross into the next section ends there, which is also a
// bar line). With no sections, `now`.
double NextBarTime(const std::vector<TempoSection>& map, double now);

// The next phrase end (every `bars_per_phrase` bars, counted from the start of
// the section) at or after `now`; a phrase cut short by a tempo change ends
// where the next section begins.
double NextPhraseTime(const std::vector<TempoSection>& map, double now,
                      int bars_per_phrase);

class LayerFader {
 public:
  // Fades `layer` to `target` (0..1) over `seconds` (0 or less: at once). A
  // layer not seen before starts at 0.
  void Fade(const std::string& layer, double target, double seconds);

  // Fades the layers named to 1 and every other known layer to 0, over
  // `seconds`: a crossfade between sets of layers.
  void Crossfade(const std::vector<std::string>& to, double seconds);

  // The same, but starting at the music's own time `at` (seconds since the
  // fader was created or cleared; see Time()): the fade waits until then. A
  // time already past starts at once. Use NextBarTime so a layer comes in on a
  // bar line.
  void FadeAt(const std::string& layer, double target, double seconds,
              double at);
  void CrossfadeAt(const std::vector<std::string>& to, double seconds,
                   double at);

  void Update(double dt);

  // Seconds of music so far: the clock the `...At` calls are on.
  [[nodiscard]] double Time() const { return time_; }

  // The current gain of `layer`, 0 for an unknown one.
  [[nodiscard]] double Gain(const std::string& layer) const;
  // Whether `layer` is, or is heading to, audible.
  [[nodiscard]] bool Wanted(const std::string& layer) const;
  // Every layer has faded out and none is wanted.
  [[nodiscard]] bool Silent() const;

  void Clear() {
    layers_.clear();
    pending_.clear();
    time_ = 0;
  }

 private:
  struct State {
    double gain = 0, target = 0, rate = 0;  // rate: gain units per second
  };
  struct Pending {
    double at;
    std::string layer;
    std::vector<std::string> to;  // a crossfade when `layer` is empty
    double target, seconds;
  };
  std::map<std::string, State> layers_;
  std::vector<Pending> pending_;
  double time_ = 0;
};
}

#endif
