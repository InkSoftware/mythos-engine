#include "engine/gfx/morph.h"

#include <algorithm>
#include <cmath>

namespace mythos {
std::vector<float> MorphTrack::Sample(const double t) const {
  if (targets == 0 || times.empty() || weights.size() < times.size() * targets)
    return {};
  const auto key = [&](const size_t i) {
    return std::vector<float>(
        weights.begin() + static_cast<std::ptrdiff_t>(i * targets),
        weights.begin() + static_cast<std::ptrdiff_t>((i + 1) * targets));
  };
  if (t <= times.front()) return key(0);
  if (t >= times.back()) return key(times.size() - 1);
  const size_t hi =
      static_cast<size_t>(std::ranges::upper_bound(times, t) - times.begin());
  const size_t lo = hi - 1;
  const double span = times[hi] - times[lo];
  const auto f = static_cast<float>(span > 1e-12 ? (t - times[lo]) / span : 0);
  std::vector<float> a = key(lo);
  const std::vector<float> b = key(hi);
  for (size_t i = 0; i < targets; ++i) a[i] += (b[i] - a[i]) * f;
  return a;
}

Batch ApplyMorph(const Batch& base, const BatchMorph& morph,
                 const std::vector<float>& weights) {
  Batch out = base;
  const size_t n = base.verts.size();
  for (size_t t = 0; t < morph.targets.size() && t < weights.size(); ++t) {
    const float w = weights[t];
    if (w == 0.0f) continue;
    const MorphTarget& target = morph.targets[t];
    if (target.position.size() == n * 3) {
      for (size_t i = 0; i < n; ++i) {
        out.verts[i].px += w * target.position[i * 3];
        out.verts[i].py += w * target.position[i * 3 + 1];
        out.verts[i].pz += w * target.position[i * 3 + 2];
      }
    }
    if (target.normal.size() == n * 3) {
      for (size_t i = 0; i < n; ++i) {
        out.verts[i].nx += w * target.normal[i * 3];
        out.verts[i].ny += w * target.normal[i * 3 + 1];
        out.verts[i].nz += w * target.normal[i * 3 + 2];
      }
    }
  }
  for (Vertex& v : out.verts) {
    const float len = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
    if (len > 1e-6f) {
      v.nx /= len;
      v.ny /= len;
      v.nz /= len;
    }
  }
  return out;
}
}
