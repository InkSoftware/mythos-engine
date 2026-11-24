#include "engine/audio/music.h"

#include <algorithm>
#include <cmath>

namespace mythos {
void LayerFader::Fade(const std::string& layer, const double target,
                      const double seconds) {
  State& s = layers_[layer];
  s.target = std::clamp(target, 0.0, 1.0);
  if (seconds <= 0.0) {
    s.gain = s.target;
    s.rate = 0.0;
  } else {
    s.rate = std::fabs(s.target - s.gain) / seconds;
  }
}

void LayerFader::Crossfade(const std::vector<std::string>& to,
                           const double seconds) {
  for (const auto& entry : layers_) {
    if (std::ranges::find(to, entry.first) == to.end()) {
      Fade(entry.first, 0.0, seconds);
    }
  }
  for (const std::string& layer : to) Fade(layer, 1.0, seconds);
}

void LayerFader::Update(const double dt) {
  for (auto& entry : layers_) {
    State& s = entry.second;
    const double step = s.rate * std::max(0.0, dt);
    if (s.gain < s.target) {
      s.gain = std::min(s.target, s.gain + step);
    } else if (s.gain > s.target) {
      s.gain = std::max(s.target, s.gain - step);
    }
  }
}

double LayerFader::Gain(const std::string& layer) const {
  const auto it = layers_.find(layer);
  return it == layers_.end() ? 0.0 : it->second.gain;
}

bool LayerFader::Wanted(const std::string& layer) const {
  const auto it = layers_.find(layer);
  return it != layers_.end() &&
         (it->second.target > 0.0 || it->second.gain > 0.0);
}

bool LayerFader::Silent() const {
  return std::ranges::none_of(layers_, [](const auto& entry) {
    return entry.second.target > 0.0 || entry.second.gain > 0.0;
  });
}
}
