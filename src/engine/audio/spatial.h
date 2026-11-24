// Headless sound-space arithmetic beyond panning: how much a sound is muffled
// by what is between it and the listener (occlusion), how much of it goes to
// a reverb for the size of the room (a send), and mixer groups with volume
// and ducking. The platform mixer applies the numbers; nothing here touches
// audio devices.
#ifndef MYTHOS_ENGINE_AUDIO_SPATIAL_H_
#define MYTHOS_ENGINE_AUDIO_SPATIAL_H_

#include <map>
#include <string>
#include <vector>

namespace mythos {
// What the walls and closed doors between a source and the listener do to it.
struct OcclusionParams {
  // Gain left after each wall and each closed door.
  double wall_gain = 0.45;
  double door_gain = 0.7;
  // How much of the high end is lost per wall and per closed door (0 none,
  // 1 all of it); it adds up to at most 1.
  double wall_muffle = 0.45;
  double door_muffle = 0.3;
};

struct Occlusion {
  // Linear gain multiplier, 1 in the open.
  double gain = 1;
  // 0 clear .. 1 fully muffled: how far to close a low-pass filter.
  double muffle = 0;
};

Occlusion ComputeOcclusion(int walls, int closed_doors,
                           const OcclusionParams& params = {});

// The cutoff in Hz for a muffle amount, from `open_hz` (clear) down to
// `closed_hz` (fully muffled), spaced evenly in pitch (octaves).
double MuffleCutoffHz(double muffle, double open_hz = 16000,
                      double closed_hz = 500);

// One-pole low-pass smoothing coefficient (0..1, the weight of the new
// sample) for a cutoff at a sample rate: y += a * (x - y).
double LowpassCoefficient(double cutoff_hz, double sample_rate);

// Reverb for a room: how much of a sound is sent to the reverb (a bigger room
// sends more) and how long the tail rings, from the number of cells of floor
// a room has.
struct ReverbSend {
  double send = 0;  // 0..1
  double decay_seconds = 0;
};
ReverbSend ReverbForRoom(int room_cells);

// Mixer groups ("music", "effects", "voice"): a volume each, and ducking one
// group while another is active (music down while a line is spoken). Smooth:
// call Update every frame.
class MixerGroups {
 public:
  void SetVolume(const std::string& group, double volume);
  [[nodiscard]] double Volume(const std::string& group) const;

  // While `by` is active (SetActive), `group` is turned down to `amount`
  // (0..1 of its volume), easing at `attack` / `release` seconds.
  void AddDuck(const std::string& group, const std::string& by, double amount,
               double attack = 0.1, double release = 0.6);
  void SetActive(const std::string& group, bool active);
  // Marks every group inactive (the mixer then marks the ones playing).
  void ClearActive() { active_.clear(); }

  void Update(double dt);

  // Volume times the duck, what the mixer multiplies by.
  [[nodiscard]] double Gain(const std::string& group) const;

 private:
  struct Duck {
    std::string group, by;
    double amount, attack, release;
    double level = 1;  // current multiplier
  };
  std::map<std::string, double> volume_;
  std::map<std::string, bool> active_;
  std::vector<Duck> ducks_;
};
}

#endif
