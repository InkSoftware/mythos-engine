#include "engine/audio/music.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace mythos {
double NextBarTime(const double now, const double bpm,
                   const int beats_per_bar) {
  if (bpm <= 0 || beats_per_bar <= 0) return now;
  const double bar = 60.0 / bpm * beats_per_bar;
  const double bars = std::ceil(now / bar - 1e-9);
  return std::max(0.0, bars) * bar;
}

namespace {
// The first multiple of `step` from `start` that is at or after `now`.
double NextOnGrid(const double start, const double step, const double now) {
  if (now <= start) return start;
  return start + std::ceil((now - start) / step - 1e-9) * step;
}

double NextOnMap(const std::vector<TempoSection>& map, const double now,
                 const int bars) {
  for (std::size_t i = 0; i < map.size(); ++i) {
    const TempoSection& s = map[i];
    const double end = i + 1 < map.size() ? map[i + 1].start
                                          : std::numeric_limits<double>::max();
    if (now > end) continue;
    if (s.bpm <= 0 || s.beats_per_bar <= 0 || bars <= 0) return now;
    const double step = 60.0 / s.bpm * s.beats_per_bar * bars;
    return std::min(NextOnGrid(s.start, step, now), end);
  }
  return now;
}
}  // namespace

double NextBarTime(const std::vector<TempoSection>& map, const double now) {
  return NextOnMap(map, now, 1);
}

double NextPhraseTime(const std::vector<TempoSection>& map, const double now,
                      const int bars_per_phrase) {
  return NextOnMap(map, now, bars_per_phrase);
}

void LayerFader::FadeAt(const std::string& layer, const double target,
                        const double seconds, const double at) {
  if (at <= time_) {
    Fade(layer, target, seconds);
    return;
  }
  layers_[layer];  // a layer the fade will touch is known from now on
  pending_.push_back({at, layer, {}, target, seconds});
}

void LayerFader::CrossfadeAt(const std::vector<std::string>& to,
                             const double seconds, const double at) {
  if (at <= time_) {
    Crossfade(to, seconds);
    return;
  }
  pending_.push_back({at, "", to, 1.0, seconds});
}

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
  time_ += std::max(0.0, dt);
  // Fades that were waiting for their time start now (in the order given).
  for (size_t i = 0; i < pending_.size();) {
    if (pending_[i].at > time_) {
      ++i;
      continue;
    }
    const Pending p = pending_[i];
    pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
    if (p.layer.empty()) {
      Crossfade(p.to, p.seconds);
    } else {
      Fade(p.layer, p.target, p.seconds);
    }
  }
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
