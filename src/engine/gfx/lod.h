// Which instances of models to draw this frame and in what order: a distance
// cull per instance, a cheaper model swapped in beyond a distance (LOD), and
// the survivors sorted by model so the renderer rebinds as little as it can.
// Headless; the renderer asks it for a plan (see ModelInstance::max_distance
// and SceneRenderer::SetModelLod).
#ifndef MYTHOS_ENGINE_GFX_LOD_H_
#define MYTHOS_ENGINE_GFX_LOD_H_

#include <cstddef>
#include <vector>

namespace mythos {
// What the planner needs to know of one instance.
struct LodRequest {
  int model = -1;
  // Distance from the eye, in metres.
  double distance = 0;
  // Not drawn beyond this distance; 0 means never culled.
  double max_distance = 0;
};

// One planned draw: the request it came from and the model to draw (which is
// a lower-detail one beyond the LOD distance).
struct LodDraw {
  size_t request = 0;
  int model = -1;
};

class ModelLods {
 public:
  // Beyond `distance` metres draw `far_model` in place of `model`. A chain
  // works (a far model may have its own); a loop is cut after a few steps.
  void Set(int model, int far_model, double distance);
  void Clear() { entries_.clear(); }

  // The model to draw at `distance`.
  [[nodiscard]] int Choose(int model, double distance) const;

 private:
  struct Entry {
    int model, far_model;
    double distance;
  };
  std::vector<Entry> entries_;
};

// Culls, swaps and sorts: instances with a negative model, or beyond their
// max_distance, are dropped; the rest come out ordered by the model drawn
// (ties keep the order given, so drawing stays deterministic).
std::vector<LodDraw> PlanDraws(const std::vector<LodRequest>& requests,
                               const ModelLods& lods);
}

#endif
