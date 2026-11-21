#ifndef MYTHOS_ENGINE_PLATFORM_GL_SCENE_SHADERS_H_
#define MYTHOS_ENGINE_PLATFORM_GL_SCENE_SHADERS_H_

// GLSL sources, embedded at build time from `shaders/vertex` and
// `shaders/fragment` into a generated source file (cmake/EmbedShaders.cmake).
// All are `#version 410 core`.
namespace mythos::shaders {
extern const char* const kWorldGeometryVert;
extern const char* const kFullscreenQuadVert;
extern const char* const kMirrorGlassVert;

extern const char* const kWorldLightingFrag;
extern const char* const kShadowDistanceFrag;
extern const char* const kNightSkyFrag;
extern const char* const kToneMapAndGrainFrag;
extern const char* const kLensAndFxaaFrag;
extern const char* const kMirrorGlassFrag;
extern const char* const kOverlayFrag;
extern const char* const kBloomBlurFrag;
}

#endif
