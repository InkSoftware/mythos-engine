#ifndef MYTHOS_ENGINE_GFX_NORMALMAP_H_
#define MYTHOS_ENGINE_GFX_NORMALMAP_H_

#include "engine/gfx/surface.h"

// Normal maps made from the textures themselves. The brightness of a texture is
// read as a height (mortar is low, brick high; baked occlusion is a dent) and
// its slope becomes the tilt of the surface. Pure CPU; the renderer uploads the
// result beside the texture.
namespace mythos {
// Returns a tangent-space normal map the size of `albedo` (empty for an empty
// input): RGB = (n * 0.5 + 0.5) * 255, alpha 255. x points along the image's +x
// (u), y toward the top of the image (v, GL convention), z out of the surface.
// The image wraps at its edges because textures repeat. `strength` scales the
// slope: 0 gives a flat map (128, 128, 255), larger values tilt the normal more
// for the same change in height.
Surface MakeNormalMap(const Surface& albedo, float strength);
}

#endif
