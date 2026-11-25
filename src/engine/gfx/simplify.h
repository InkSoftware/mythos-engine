// Simplifying static world geometry for a far level of detail: the mesher
// emits a quad a cell, so a long wall, a floor or a ceiling is hundreds of
// triangles that a distant eye cannot tell from a few. `SimplifyBatches` joins
// axis-aligned quads that lie in one plane, face the same way, share a whole
// edge and carry one continuous (repeating) texture mapping into larger ones;
// anything else passes through untouched. Headless.
#ifndef MYTHOS_ENGINE_GFX_SIMPLIFY_H_
#define MYTHOS_ENGINE_GFX_SIMPLIFY_H_

#include <cstddef>
#include <vector>

#include "engine/gfx/mesh.h"

namespace mythos {
// What a simplification did.
struct SimplifyStats {
  size_t triangles_before = 0;
  size_t triangles_after = 0;
};

// Rewrites each static (unskinned) batch of `batches`. Two quads merge only
// when the renderer would keep them in one chunk: with `column` > 0 (8, the
// renderer's chunk size, by default) a merged quad stays in the 8 x 8 cell
// column its pieces were in, so the far set still lines up with the full one
// column by column (`SceneRenderer::UploadWorldLod`). The mapping repeats, so
// the picture is the same as the cell quads'. Neighbouring quads that did not
// merge can leave a hairline at a T-junction.
SimplifyStats SimplifyBatches(std::vector<Batch>& batches, float column = 8.0f);

// The world and the props of a scene geometry (not the decals).
SimplifyStats SimplifyGeometry(SceneGeometry& geometry, float column = 8.0f);

// The far set for `SceneRenderer::UploadWorldLod`: the level built with
// `GeometryDetail::kFar` (no window frames, no decals) and then simplified.
// Pass the library of cheaper models (`ModelLibrary::BoundingBoxes()`).
SceneGeometry BuildFarGeometry(const Level& level, const ModelLibrary& models,
                               bool outdoor = false, float column = 8.0f);
}

#endif
