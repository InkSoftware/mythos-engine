#ifndef MYTHOS_ENGINE_GFX_PBR_H_
#define MYTHOS_ENGINE_GFX_PBR_H_

#include <string>

#include "engine/gfx/surface.h"

// PBR (metallic-roughness) companion maps. A texture "m_sofa_0" may be
// accompanied by textures registered in the TextureLibrary like any other:
//   m_sofa_0_nrm    tangent-space normal map (glTF convention, +y up)
//   m_sofa_0_rough  grayscale roughness (255 = fully rough)
//   m_sofa_0_metal  grayscale metalness (255 = metal)
//   m_sofa_0_ao     grayscale ambient occlusion (255 = open)
// The green channel of each grayscale map is read. Without companions a
// surface keeps the normal map made from its own brightness (normalmap.h),
// with no specular, metal or occlusion.
//
// The renderer uploads two RGBA maps per texture: the surface map (RGB =
// normal, alpha = gloss, 255 - roughness; 0 is matt) and the material map (R =
// metal, G = occlusion). Pure CPU.
namespace mythos {
// Name suffixes of the companion maps.
inline constexpr const char* kNormalSuffix = "_nrm";
inline constexpr const char* kRoughSuffix = "_rough";
inline constexpr const char* kMetalSuffix = "_metal";
inline constexpr const char* kAoSuffix = "_ao";

// True for a name ending in a companion suffix (and longer than it); such a
// texture is not drawn on its own.
bool IsPbrCompanion(const std::string& name);

// Builds the renderer's normal and gloss map for `albedo`. `normal` and `rough`
// may be null or empty; a companion of another size is resampled to the size of
// `albedo`. `strength` is MakeNormalMap's and is used only when there is no
// authored normal map. Gloss is 0 without a roughness map. Returns an empty
// Surface for an empty `albedo`.
Surface MakeSurfaceMap(const Surface& albedo, const Surface* normal,
                       const Surface* rough, float strength);

// Builds the material map of `albedo`: R = metal, G = occlusion (B and alpha
// 255). Without a metal map the metalness is 0; without an occlusion map the
// surface is fully open (255). Returns an empty Surface when both are null or
// empty, so the renderer can share one default for the many textures that have
// neither.
Surface MakeMaterialMap(const Surface& albedo, const Surface* metal,
                        const Surface* ao);
}

#endif
