#pragma once
// Normal maps made from the textures themselves. The art has no hand-made normal maps, so the
// brightness of a texture is read as a height (mortar is low, brick high; baked ambient occlusion
// is a dent) and its slope becomes the tilt of the surface. Pure CPU, headless and tested; the
// renderer uploads the result beside the texture.

#include "engine/gfx/surface.h"

namespace mythos {

// Returns a tangent-space normal map the size of albedo: RGB = (n * 0.5 + 0.5) * 255, alpha 255.
// x points along the image's +x (u), y towards the top of the image (v, GL convention), z out of
// the surface. The image wraps at its edges (textures repeat). strength scales the slope: 0 gives
// a flat map (128, 128, 255); larger values tilt the normal more for the same change in height.
Surface makeNormalMap(const Surface& albedo, float strength);

}  // namespace mythos
