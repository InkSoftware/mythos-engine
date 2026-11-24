#include "engine/gfx/lod.h"

#include <algorithm>

namespace mythos {
void ModelLods::Set(const int model, const int far_model,
                    const double distance) {
  for (Entry& e : entries_) {
    if (e.model == model) {
      e.far_model = far_model;
      e.distance = distance;
      return;
    }
  }
  entries_.push_back({model, far_model, distance});
}

int ModelLods::Choose(int model, const double distance) const {
  // A few hops at most, so a loop in the table cannot hang a frame.
  for (int hop = 0; hop < 4; ++hop) {
    const auto it = std::ranges::find_if(
        entries_, [&](const Entry& e) { return e.model == model; });
    if (it == entries_.end() || distance <= it->distance) break;
    model = it->far_model;
  }
  return model;
}

std::vector<LodDraw> PlanDraws(const std::vector<LodRequest>& requests,
                               const ModelLods& lods) {
  std::vector<LodDraw> plan;
  plan.reserve(requests.size());
  for (size_t i = 0; i < requests.size(); ++i) {
    const LodRequest& r = requests[i];
    if (r.model < 0) continue;
    if (r.max_distance > 0 && r.distance > r.max_distance) continue;
    plan.push_back({i, lods.Choose(r.model, r.distance)});
  }
  std::ranges::stable_sort(plan, [](const LodDraw& a, const LodDraw& b) {
    return a.model < b.model;
  });
  return plan;
}
}
