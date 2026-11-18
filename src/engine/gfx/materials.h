#ifndef MYTHOS_ENGINE_GFX_MATERIALS_H_
#define MYTHOS_ENGINE_GFX_MATERIALS_H_
// Procedural materials any game on this engine can use: brick, wood, tile,
// carpet, stone, grass, rock, pine, plaster, paper, cloth, enamel, metal,
// asphalt, wallpapers, a door, a mirror, black "void", and magenta PLACEHOLDER
// posters and paintings. They are fallbacks: an image (PNG, BMP, ...) of the
// same name in the asset folder overrides each one
// (TextureLibrary::loadOverrides). A game calls this first, then registers its
// own story-specific art, replacing any name it likes.

#include "engine/gfx/textures.h"

namespace mythos {

void AddCommonMaterials(TextureLibrary& lib);

}  // namespace mythos

#endif  // MYTHOS_ENGINE_GFX_MATERIALS_H_
