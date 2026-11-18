#pragma once
// The GLSL of the scene renderer (OpenGL 4.1 core, `#version 410 core`). The sources are in
// shaders/vertex and shaders/fragment and are embedded at build time (cmake/EmbedShaders.cmake).
// kToneMapAndGrainFrag is appended to the world and sky fragment shaders so they match.
namespace mythos::shaders {

// Vertex shaders
extern const char* const kWorldGeometryVert;    // world models; also the shadow pass's vertex shader
extern const char* const kFullscreenQuadVert;   // a fullscreen quad
extern const char* const kMirrorGlassVert;      // the mirror's quad

// Fragment shaders
extern const char* const kWorldLightingFrag;    // lighting, lamps, hand light, shadows, fog
extern const char* const kShadowDistanceFrag;   // distance from the lamp, into a cube face
extern const char* const kNightSkyFrag;         // night sky
extern const char* const kToneMapAndGrainFrag;  // shared: tone mapping, vignette, grain
extern const char* const kLensAndFxaaFrag;      // motion blur, lens, FXAA
extern const char* const kMirrorGlassFrag;      // the mirror's picture, looked up by screen position
extern const char* const kOverlayFrag;          // a plain textured quad (the overlay)

}  // namespace mythos::shaders
