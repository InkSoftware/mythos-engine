#ifndef MYTHOS_ENGINE_GFX_MATERIALS_H_
#define MYTHOS_ENGINE_GFX_MATERIALS_H_

#include "engine/gfx/textures.h"

// Procedural materials any game on this engine can use: brick, wood, tile,
// carpet, stone, grass, rock, pine, plaster, paper, cloth, enamel, metal,
// asphalt, wallpapers, a door, a mirror, a black "void", and magenta
// PLACEHOLDER poster and painting. They are fallbacks: an image of the same
// name in the asset folder overrides each one (TextureLibrary::LoadOverrides).
//
// Registers all of them in `lib`. A game calls this first, then registers its
// own art, replacing any name it likes.
namespace mythos {
void AddCommonMaterials(TextureLibrary& lib);
}

#endif
