#ifndef MYTHOS_ENGINE_PLATFORM_GL_SCENE_SHADERS_H_
#define MYTHOS_ENGINE_PLATFORM_GL_SCENE_SHADERS_H_

// GLSL sources, embedded at build time from `shaders/vertex` and
// `shaders/fragment` into a generated source file (cmake/EmbedShaders.cmake).
// All are `#version 410 core`.
namespace mythos::shaders {
// Each embedded source with the path of the file it came from, relative to
// the shaders folder (for loading loose files in development).
struct ShaderFile {
  const char* path;
  const char* const* source;
};
extern const ShaderFile kShaderFiles[];
extern const int kShaderFileCount;

extern const char* const kWorldGeometryVert;
extern const char* const kFullscreenQuadVert;
extern const char* const kMirrorGlassVert;

extern const char* const kWorldLightingFrag;
extern const char* const kShadowDistanceFrag;
extern const char* const kNightSkyFrag;
extern const char* const kToneMapAndGrainFrag;
extern const char* const kLensAndFxaaFrag;
extern const char* const kMirrorGlassFrag;
extern const char* const kGlassPaneVert;
extern const char* const kParticleVert;
extern const char* const kParticleFrag;
extern const char* const kGlassPaneFrag;
extern const char* const kOverlayFrag;
extern const char* const kBloomBlurFrag;
extern const char* const kSsaoFrag;
extern const char* const kProbeFilterFrag;
}

#endif
