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
class LayerFader {
 public:
  // Fades `layer` to `target` (0..1) over `seconds` (0 or less: at once). A
  // layer not seen before starts at 0.
  void Fade(const std::string& layer, double target, double seconds);

  // Fades the layers named to 1 and every other known layer to 0, over
  // `seconds`: a crossfade between sets of layers.
  void Crossfade(const std::vector<std::string>& to, double seconds);

  void Update(double dt);

  // The current gain of `layer`, 0 for an unknown one.
  [[nodiscard]] double Gain(const std::string& layer) const;
  // Whether `layer` is, or is heading to, audible.
  [[nodiscard]] bool Wanted(const std::string& layer) const;
  // Every layer has faded out and none is wanted.
  [[nodiscard]] bool Silent() const;

  void Clear() { layers_.clear(); }

 private:
  struct State {
    double gain = 0, target = 0, rate = 0;  // rate: gain units per second
  };
  std::map<std::string, State> layers_;
};
}

#endif
