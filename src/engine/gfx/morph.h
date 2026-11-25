// Morph targets (blend shapes): a batch's vertices moved toward named targets
// by weights. glTF's targets are deltas from the base mesh; `ApplyMorph` adds
// the weighted deltas on the CPU and returns a batch to upload (a skinned batch
// keeps its skin, so morphing happens before the bones). For a model that
// morphs, a game calls it when the weights change and swaps the result into the
// uploaded model with `SceneRenderer::ReplaceModel`: it costs a re-upload, so
// it suits a few small models (a face), not a crowd. Headless.
#ifndef MYTHOS_ENGINE_GFX_MORPH_H_
#define MYTHOS_ENGINE_GFX_MORPH_H_

#include <cstddef>
#include <string>
#include <vector>

#include "engine/gfx/mesh.h"

namespace mythos {
// One target of a batch: a delta per vertex of the batch (three floats each,
// aligned to `Batch::verts`; empty means no change).
struct MorphTarget {
  std::vector<float> position;
  std::vector<float> normal;
};

// The targets of one batch and the weights the file gives them at rest.
struct BatchMorph {
  std::vector<MorphTarget> targets;
  std::vector<float> default_weights;
  [[nodiscard]] bool Empty() const { return targets.empty(); }
};

// Weights over time for the batches [first_batch, first_batch + batch_count)
// of a model: `weights` holds `targets` values for each time, in order. It
// belongs to the clip `clip` of the model (`GltfModel::clips`).
struct MorphTrack {
  size_t clip = 0;
  size_t first_batch = 0, batch_count = 0;
  size_t targets = 0;
  std::vector<double> times;
  std::vector<float> weights;

  // The weights at time `t` (clamped to the first and last key, linear
  // between), `targets` of them; empty for a track with no keys.
  [[nodiscard]] std::vector<float> Sample(double t) const;
};

// `base` with every vertex moved by `sum(weights[i] * targets[i])` (positions
// and normals; the normals are normalised again). Missing weights count as 0
// and extra ones are ignored; a target that does not match the batch is
// skipped.
[[nodiscard]] Batch ApplyMorph(const Batch& base, const BatchMorph& morph,
                               const std::vector<float>& weights);
}

#endif
