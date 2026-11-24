#include "engine/audio/spatial.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mythos {
Occlusion ComputeOcclusion(const int walls, const int closed_doors,
                           const OcclusionParams& params) {
  const int w = std::max(0, walls), d = std::max(0, closed_doors);
  Occlusion out;
  out.gain = std::pow(params.wall_gain, w) * std::pow(params.door_gain, d);
  out.muffle = std::min(1.0, w * params.wall_muffle + d * params.door_muffle);
  return out;
}

double MuffleCutoffHz(const double muffle, const double open_hz,
                      const double closed_hz) {
  const double m = std::clamp(muffle, 0.0, 1.0);
  return open_hz * std::pow(closed_hz / open_hz, m);
}

double LowpassCoefficient(const double cutoff_hz, const double sample_rate) {
  if (sample_rate <= 0 || cutoff_hz <= 0) return 0;
  const double x = -2.0 * std::numbers::pi * cutoff_hz / sample_rate;
  return 1.0 - std::exp(x);
}

ReverbSend ReverbForRoom(const int room_cells) {
  // A closet barely rings; a hall (a few hundred cells) rings for seconds.
  const double size = std::clamp(room_cells, 0, 400) / 400.0;
  ReverbSend out;
  out.send = 0.05 + 0.45 * std::sqrt(size);
  out.decay_seconds = 0.2 + 2.3 * size;
  return out;
}

void MixerGroups::SetVolume(const std::string& group, const double volume) {
  volume_[group] = std::clamp(volume, 0.0, 1.0);
}

double MixerGroups::Volume(const std::string& group) const {
  const auto it = volume_.find(group);
  return it == volume_.end() ? 1.0 : it->second;
}

void MixerGroups::AddDuck(const std::string& group, const std::string& by,
                          const double amount, const double attack,
                          const double release) {
  ducks_.push_back({group, by, std::clamp(amount, 0.0, 1.0),
                    std::max(attack, 1e-3), std::max(release, 1e-3)});
}

void MixerGroups::SetActive(const std::string& group, const bool active) {
  active_[group] = active;
}

void MixerGroups::Update(const double dt) {
  for (Duck& d : ducks_) {
    const auto it = active_.find(d.by);
    const bool on = it != active_.end() && it->second;
    const double target = on ? d.amount : 1.0;
    const double span = on ? d.attack : d.release;
    // Linear ease; the whole travel from 1 to `amount` takes `span`.
    const double step = (1.0 - d.amount) * dt / span;
    if (d.level > target) {
      d.level = std::max(target, d.level - step);
    } else {
      d.level = std::min(target, d.level + step);
    }
  }
}

double MixerGroups::Gain(const std::string& group) const {
  double gain = Volume(group);
  for (const Duck& d : ducks_) {
    if (d.group == group) gain *= d.level;
  }
  return gain;
}
}
