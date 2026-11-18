#ifndef MYTHOS_ENGINE_GFX_PBR_H_
#define MYTHOS_ENGINE_GFX_PBR_H_
// PBR (metallic-roughness): optional per-texture companion maps. A texture
// "m_sofa_0" may be accompanied by
//   m_sofa_0_nrm    a tangent-space normal map (glTF / OpenGL convention: +y
//   up), m_sofa_0_rough  a greyscale roughness map (255 = fully rough; the
//   green channel is read), m_sofa_0_metal  a greyscale metalness map (255 =
//   metal; the green channel is read), m_sofa_0_ao     a greyscale
//   ambient-occlusion map (255 = open; the green channel is read),
// registered in the TextureLibrary like any texture. Without them the surface
// keeps the normal map made from its own brightness (gfx/normalmap.h), no
// specular, no metal and no occlusion, as before. The renderer uploads two RGBA
// maps per texture: surface (RGB = the normal, alpha = gloss, 255 - roughness;
// 0 = matt, no specular) and material (R = metal, G = occlusion). Pure CPU,
// headless and tested.

#include <string>

#include "engine/gfx/surface.h"

namespace mythos {

inline constexpr const char* kNormalSuffix = "_nrm";
inline constexpr const char* kRoughSuffix = "_rough";
inline constexpr const char* kMetalSuffix = "_metal";
inline constexpr const char* kAoSuffix = "_ao";

// True for a name ending in a companion suffix (such a texture is not drawn on
// its own).
bool IsPbrCompanion(const std::string& name);

// Builds the renderer's normal + gloss map for albedo. normal / rough may be
// null (or empty); a companion of another size is resampled to albedo's.
// strength is makeNormalMap's, used only when there is no authored normal map.
Surface MakeSurfaceMap(const Surface& albedo, const Surface* normal,
                       const Surface* rough, float strength);

// The material map of albedo: R = metal, G = ambient occlusion (B and alpha
// 255). Without a metal map the metalness is 0, without an occlusion map the
// surface is fully open (255). Returns an empty Surface when both are null or
// empty, so the renderer can share one default for the many textures that have
// neither.
Surface MakeMaterialMap(const Surface& albedo, const Surface* metal,
                        const Surface* ao);

}  // namespace mythos

#endif  // MYTHOS_ENGINE_GFX_PBR_H_
