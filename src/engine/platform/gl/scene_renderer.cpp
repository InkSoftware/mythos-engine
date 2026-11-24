#include "engine/platform/gl/scene_renderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <numbers>
#include <utility>

#include "engine/core/fileio.h"
#include "engine/core/mat4.h"
#include "engine/core/math.h"
#include "engine/gfx/normalmap.h"
#include "engine/gfx/pbr.h"
#include "engine/platform/gl/scene_shaders.h"
#include "engine/ui/settings.h"

namespace mythos {
namespace {
using gl::ActiveTexture;
using gl::BindBuffer;
using gl::BindTexture;
using gl::BindVertexArray;
using gl::BlendFunc;
using gl::Clear;
using gl::ClearColor;
using gl::DepthFunc;
using gl::DepthMask;
using gl::Disable;
using gl::DrawArrays;
using gl::Enable;
using gl::EnableVertexAttribArray;
using gl::GenerateMipmap;
using gl::GLenum;
using gl::GLsizei;
using gl::GLuint;
using gl::kArrayBuffer;
using gl::kBgra;
using gl::kBlend;
using gl::kClampToEdge;
using gl::kColorBufferBit;
using gl::kDepthBufferBit;
using gl::kDepthTest;
using gl::kDynamicDraw;
using gl::kFalse;
using gl::kFloat;
using gl::kLequal;
using gl::kLinear;
using gl::kLinearMipmapLinear;
using gl::kNearest;
using gl::kOne;
using gl::kOneMinusSrcAlpha;
using gl::kRepeat;
using gl::kRgba8;
using gl::kSrcAlpha;
using gl::kSrgb8Alpha8;
using gl::kStaticDraw;
using gl::kTexture0;
using gl::kTexture2D;
using gl::kTextureCubeMap;
using gl::kTriangles;
using gl::kTrue;
using gl::kUnsignedInt8888Rev;
using gl::TexImage2D;
using gl::TexSubImage2D;
using gl::Uniform1f;
using gl::Uniform1fv;
using gl::Uniform1i;
using gl::Uniform2f;
using gl::Uniform2fv;
using gl::Uniform3f;
using gl::Uniform3fv;
using gl::Uniform4f;
using gl::Uniform4fv;
using gl::UniformMatrix4fv;
using gl::VertexAttribPointer;
using gl::Viewport;

// Model matrix of a door: a rotation about the y axis at the hinge.
void DoorModel(const double hinge_x, const double hinge_y, const double hinge_z,
               const double angle, float* const m) {
  const auto c = static_cast<float>(std::cos(angle)),
             s = static_cast<float>(std::sin(angle));
  Mat4Identity(m);
  m[0] = c;
  m[2] = s;
  m[8] = -s;
  m[10] = c;
  m[12] = static_cast<float>(hinge_x);
  m[13] = static_cast<float>(hinge_y);
  m[14] = static_cast<float>(hinge_z);
}

// Vertical field of view and clip planes of the main view.
constexpr float kFovY = 62.0f * std::numbers::pi_v<float> / 180.0f;
constexpr float kNearPlane = 0.05f, kFarPlane = 80.0f;
constexpr double kTwoPi = 6.283185307179586;
constexpr double kHalfPi = 1.5707963;
constexpr float kBoundsEmpty = 1e30f;
constexpr float kRoomFogDensity = 0.075f, kOutdoorFogDensity = 0.040f;
constexpr double kMaxHandPitch = 1.5;

// Slack added to a door's culling box.
constexpr float kDoorCullMargin = 0.1f;

// Hinge placement within a doorway cell: across the cell, and along it a base
// position shifted by the swing side.
constexpr double kHingeAcross = 0.175, kHingeBase = 0.5, kHingeSwingShift = 0.2;

// The mirror is skipped when the eye is nearly in its plane. The clip plane is
// pulled 1.5 cm behind the glass to hide the wall and prop face, and the glass
// quad sits 4 mm in front of the surface to avoid z-fighting.
constexpr double kMirrorMinFront = 0.02;
constexpr float kMirrorClipOffset = 0.015f;

constexpr float kMirrorGlassOffset = 0.004f;
constexpr int kMirrorMinSide = 16;

// The mirror target is 3/4 of the main target's size.
constexpr int kMirrorScaleNum = 3, kMirrorScaleDen = 4;

// Largest smear, as a fraction of the screen, at motion blur strength 1.
constexpr double kMotionBlurCap = 0.08;

// Strength of normal maps made from texture brightness.
constexpr float kNormalStrength = 2.5f;
// Chunk edge in world units for culling.
constexpr float kChunk = 8.0f;
// Vertex layout: position (3 floats), normal (3), uv (2).
// Texture units: color 0, lamp shadow cubes 1-4, hand shadow cube 5, then the
// surface (normal and gloss) map and the material map.
constexpr int kNormalUnit = 6, kMaterialUnit = 7;
constexpr int kFirstShadowUnit = 1, kHandShadowUnit = 5;

// A skinned vertex is a Vertex followed by SkinWeights: joints (4 floats) and
// weights (4 floats).
constexpr GLsizei kSkinnedVertexStride = 64;

struct SkinnedVertex {
  Vertex v;
  SkinWeights skin;
};
static_assert(sizeof(SkinnedVertex) == kSkinnedVertexStride);

void SetVertexLayout(const GLuint vbo, const bool skinned = false) {
  BindBuffer(kArrayBuffer, vbo);
  if (skinned) {
    EnableVertexAttribArray(3);
    VertexAttribPointer(3, 4, kFloat, kFalse, kSkinnedVertexStride,
                        reinterpret_cast<const void*>(32));
    EnableVertexAttribArray(4);
    VertexAttribPointer(4, 4, kFloat, kFalse, kSkinnedVertexStride,
                        reinterpret_cast<const void*>(48));
  }
  const GLsizei kVertexStride = skinned ? kSkinnedVertexStride : 32;
  EnableVertexAttribArray(0);
  VertexAttribPointer(0, 3, kFloat, kFalse, kVertexStride, nullptr);
  EnableVertexAttribArray(1);
  VertexAttribPointer(1, 3, kFloat, kFalse, kVertexStride,
                      reinterpret_cast<const void*>(12));
  EnableVertexAttribArray(2);
  VertexAttribPointer(2, 2, kFloat, kFalse, kVertexStride,
                      reinterpret_cast<const void*>(24));
}

// Particle vertex: position (3 floats), corner uv (2), colour (4), shape (1).
constexpr GLsizei kParticleFloats = 10;
constexpr GLsizei kParticleStride = kParticleFloats * sizeof(float);

void SetParticleLayout(const GLuint vbo) {
  BindBuffer(kArrayBuffer, vbo);
  EnableVertexAttribArray(0);
  VertexAttribPointer(0, 3, kFloat, kFalse, kParticleStride, nullptr);
  EnableVertexAttribArray(1);
  VertexAttribPointer(1, 2, kFloat, kFalse, kParticleStride,
                      reinterpret_cast<const void*>(12));
  EnableVertexAttribArray(2);
  VertexAttribPointer(2, 4, kFloat, kFalse, kParticleStride,
                      reinterpret_cast<const void*>(20));
  EnableVertexAttribArray(3);
  VertexAttribPointer(3, 1, kFloat, kFalse, kParticleStride,
                      reinterpret_cast<const void*>(36));
}

// Empty box that any point grows.
void ResetBounds(float* const bmin, float* const bmax) {
  for (int k = 0; k < 3; ++k) {
    bmin[k] = kBoundsEmpty;
    bmax[k] = -kBoundsEmpty;
  }
}

void GrowBounds(float* const bmin, float* const bmax,
                const float* const other_min, const float* const other_max) {
  for (int k = 0; k < 3; ++k) {
    bmin[k] = std::min(bmin[k], other_min[k]);
    bmax[k] = std::max(bmax[k], other_max[k]);
  }
}
}

const char* SceneRenderer::ShaderSource(const char* const embedded) const {
  if (loose_shaders_.empty()) return embedded;
  for (int i = 0; i < shaders::kShaderFileCount; ++i) {
    if (*shaders::kShaderFiles[i].source != embedded) continue;
    const auto it = loose_shaders_.find(shaders::kShaderFiles[i].path);
    return it == loose_shaders_.end() ? embedded : it->second.c_str();
  }
  return embedded;
}

// Builds every program (from the loose files when there are any). A program
// that fails to build leaves the one it would replace in place, so a typo
// during a reload does not black out the screen; returns whether all built.
bool SceneRenderer::BuildPrograms() {
  bool all = true;
  const auto build = [&](GlProgram& program, const char* const vertex,
                         const char* const fragment,
                         const std::initializer_list<GlProgram::Attrib> attribs,
                         const char* const extra = nullptr) {
    GlProgram next;
    if (next.Build(ShaderSource(vertex), ShaderSource(fragment), attribs,
                   extra != nullptr ? ShaderSource(extra) : nullptr)) {
      program = std::move(next);
    } else {
      all = false;
    }
  };
  const std::initializer_list<GlProgram::Attrib> world_attribs = {
      {0, "aPos"}, {1, "aNormal"}, {2, "aUV"}, {3, "aJoints"}, {4, "aWeights"}};
  const std::initializer_list<GlProgram::Attrib> quad_attribs = {{0, "aPos"},
                                                                 {2, "aUV"}};
  build(world_program_, shaders::kWorldGeometryVert,
        shaders::kWorldLightingFrag, world_attribs,
        shaders::kToneMapAndGrainFrag);
  build(shadow_program_, shaders::kWorldGeometryVert,
        shaders::kShadowDistanceFrag, world_attribs);
  build(composite_program_, shaders::kFullscreenQuadVert,
        shaders::kLensAndFxaaFrag, quad_attribs);
  build(quad_program_, shaders::kFullscreenQuadVert, shaders::kOverlayFrag,
        quad_attribs);
  build(sky_program_, shaders::kFullscreenQuadVert, shaders::kNightSkyFrag,
        quad_attribs, shaders::kToneMapAndGrainFrag);
  build(mirror_program_, shaders::kMirrorGlassVert, shaders::kMirrorGlassFrag,
        {{0, "aPos"}});
  build(glass_program_, shaders::kGlassPaneVert, shaders::kGlassPaneFrag,
        quad_attribs);
  build(particle_program_, shaders::kParticleVert, shaders::kParticleFrag,
        {{0, "aPos"}, {1, "aUV"}, {2, "aColor"}, {3, "aShape"}});
  build(bloom_program_, shaders::kFullscreenQuadVert, shaders::kBloomBlurFrag,
        quad_attribs);
  build(ssao_program_, shaders::kFullscreenQuadVert, shaders::kSsaoFrag,
        quad_attribs);
  if (world_program_.Valid()) LookUpWorldUniforms();
  return all;
}

bool SceneRenderer::ReloadShaders(const std::string& dir) {
  std::map<std::string, std::string> loose;
  for (int i = 0; i < shaders::kShaderFileCount; ++i) {
    std::string text = ReadTextFile(dir + "/" + shaders::kShaderFiles[i].path);
    if (text.empty()) {
      SDL_Log("shader reload: cannot read %s/%s", dir.c_str(),
              shaders::kShaderFiles[i].path);
      return false;
    }
    loose.emplace(shaders::kShaderFiles[i].path, std::move(text));
  }
  loose_shaders_ = std::move(loose);
  ++scene_revision_;  // the shadow maps were drawn by the old program
  return BuildPrograms();
}

bool SceneRenderer::Init(const TextureLibrary& textures) {
  BuildPrograms();
  const bool text_ok = text_.Init();
  BuildFullscreenQuad();
  UploadTextures(textures);

  mirror_vbo_.Upload(nullptr, 6 * sizeof(Vertex), kDynamicDraw);
  mirror_vao_.Create();
  mirror_vao_.Bind();
  SetVertexLayout(mirror_vbo_.Id());
  BindVertexArray(0);
  glass_vbo_.Upload(nullptr, 6 * sizeof(Vertex), kDynamicDraw);
  glass_vao_.Create();
  glass_vao_.Bind();
  SetVertexLayout(glass_vbo_.Id());
  BindVertexArray(0);
  particle_vbo_.Upload(nullptr, 0, kDynamicDraw);
  particle_vao_.Create();
  particle_vao_.Bind();
  SetParticleLayout(particle_vbo_.Id());
  BindVertexArray(0);
  // The mirror view starts at half size; DrawMirrorView resizes it.
  mirror_target_.Create(kInternalW / 2, kInternalH / 2);

  overlay_tex_.Create();
  overlay_tex_.SetParams(kNearest, kNearest, kClampToEdge);

  target_.Create(kInternalW, kInternalH);
  // Bloom works at a quarter of the render size.
  bloom_a_.Create(kInternalW / 4, kInternalH / 4);
  bloom_b_.Create(kInternalW / 4, kInternalH / 4);
  ao_a_.Create(kInternalW / 2, kInternalH / 2);
  ao_b_.Create(kInternalW / 2, kInternalH / 2);
  return world_program_.Valid() && quad_program_.Valid() &&
         composite_program_.Valid() && sky_program_.Valid() && text_ok;
}

void SceneRenderer::LookUpWorldUniforms() {
  u_model_ = world_program_.Uniform("uModel");
  u_emissive_ = world_program_.Uniform("uEmissive");
  u_alpha_test_ = world_program_.Uniform("uAlphaTest");
  u_opacity_ = world_program_.Uniform("uOpacity");
  u_skinned_ = world_program_.Uniform("uSkinned");
  u_bones_ = world_program_.Uniform("uBones");
  u_decal_ = world_program_.Uniform("uDecal");
  WorldUniforms& u = world_u_;
  u.mist = world_program_.Uniform("uMist");
  u.mist_height = world_program_.Uniform("uMistHeight");
  u.light_halo = world_program_.Uniform("uLightHalo[0]");
  u.view = world_program_.Uniform("uView");
  u.proj = world_program_.Uniform("uProj");
  u.cam = world_program_.Uniform("uCam");
  u.flash_dir = world_program_.Uniform("uFlashDir");
  u.flash_on = world_program_.Uniform("uFlashOn");
  u.lighter = world_program_.Uniform("uLighter");
  u.torch_warm = world_program_.Uniform("uTorchWarm");
  u.outdoor = world_program_.Uniform("uOutdoor");
  u.moon_dir = world_program_.Uniform("uMoonDir");
  u.sun_color = world_program_.Uniform("uSunColor");
  u.sky_ambient = world_program_.Uniform("uSkyAmbient");
  u.fog_color = world_program_.Uniform("uFogColor");
  u.fog_density = world_program_.Uniform("uFogDensity");
  u.time = world_program_.Uniform("uTime");
  u.ambient_base = world_program_.Uniform("uAmbientBase");
  u.ambient_near = world_program_.Uniform("uAmbientNear");
  u.lightning = world_program_.Uniform("uLightning");
  u.zone_count = world_program_.Uniform("uZoneCount");
  u.zone_xz = world_program_.Uniform("uZoneXZ[0]");
  u.zone_y = world_program_.Uniform("uZoneY[0]");
  u.light_count = world_program_.Uniform("uLightCount");
  u.light_pos = world_program_.Uniform("uLightPos[0]");
  u.light_color = world_program_.Uniform("uLightColor[0]");
  u.light_radius = world_program_.Uniform("uLightRadius[0]");
  u.shadow_count = world_program_.Uniform("uShadowCount");
  u.shadow_texel = world_program_.Uniform("uShadowTexel");
  u.hand_shadow = world_program_.Uniform("uHandShadow");
  u.hand_pos = world_program_.Uniform("uHandPos");
  u.hand_origin = world_program_.Uniform("uHandOrigin");
  u.hand_faces = world_program_.Uniform("uHandFaces");
  u.res = world_program_.Uniform("uRes");
  u.grain = world_program_.Uniform("uGrain");
  u.exposure = world_program_.Uniform("uExposure");
  u.tonemap = world_program_.Uniform("uTonemap");
  u.vignette = world_program_.Uniform("uVignette");
  u.materials = world_program_.Uniform("uMaterials");
  u.clip = world_program_.Uniform("uClip");

  world_program_.Use();
  world_program_.Set("uTex", 0);
  world_program_.Set("uShadow0", 1);
  world_program_.Set("uShadow1", 2);
  world_program_.Set("uShadow2", 3);
  world_program_.Set("uShadow3", 4);
  world_program_.Set("uShadowHand", 5);
  world_program_.Set("uNormalTex", 6);
  world_program_.Set("uMaterialTex", 7);
}

void SceneRenderer::BuildFullscreenQuad() {
  // Two triangles; per vertex x, y, u, v.
  const float quad[24] = {
      -1, -1, 0, 0, 1, -1, 1, 0, 1,  1, 1, 1,
      -1, -1, 0, 0, 1, 1,  1, 1, -1, 1, 0, 1,
  };
  quad_vbo_.Upload(quad, sizeof(quad), kStaticDraw);
  quad_vao_.Create();
  quad_vao_.Bind();
  BindBuffer(kArrayBuffer, quad_vbo_.Id());
  EnableVertexAttribArray(0);
  VertexAttribPointer(0, 2, kFloat, kFalse, 16, nullptr);
  EnableVertexAttribArray(2);
  VertexAttribPointer(2, 2, kFloat, kFalse, 16,
                      reinterpret_cast<const void*>(8));
  BindVertexArray(0);
}

GlTexture SceneRenderer::MakeMipmappedTexture(
    const int w, const int h, const void* const rgba,
    const gl::GLenum internal_format) {
  GlTexture tex;
  tex.Create();
  tex.Allocate(w, h, rgba, internal_format);
  GenerateMipmap(kTexture2D);
  tex.SetParams(kLinearMipmapLinear, kLinear, kRepeat);
  return tex;
}

// Puts an image under `name`: into the texture already there (so the ids that
// uploaded geometry holds stay valid), else a new one.
void SceneRenderer::StoreTexture(
    std::vector<std::pair<std::string, GlTexture>>& list,
    const std::string& name, const int w, const int h, const void* const rgba,
    const gl::GLenum internal_format) {
  for (auto& [key, texture] : list) {
    if (key != name) continue;
    texture.Allocate(w, h, rgba, internal_format);
    GenerateMipmap(kTexture2D);
    return;
  }
  list.emplace_back(name, MakeMipmappedTexture(w, h, rgba, internal_format));
}

void SceneRenderer::ReloadTextures(const TextureLibrary& textures) {
  ++scene_revision_;
  UploadTextures(textures);
}

void SceneRenderer::UploadTextures(const TextureLibrary& textures) {
  std::vector<uint8_t> rgba_bytes;
  for (const auto& name : textures.Names()) {
    // Companion maps (`X_nrm`, `X_rough`, ...) are folded into `X`'s maps and
    // not uploaded on their own, unless there is no base texture.
    if (IsPbrCompanion(name)) {
      const std::string base = name.substr(0, name.rfind('_'));
      if (textures.Has(base)) continue;
    }
    const Surface& s = textures.Get(name);
    SurfaceToRgbaFlipped(s, rgba_bytes);
    StoreTexture(textures_, name, s.w, s.h, rgba_bytes.data(), kSrgb8Alpha8);

    const std::string nrm_name = name + kNormalSuffix,
                      rough_name = name + kRoughSuffix,
                      metal_name = name + kMetalSuffix,
                      ao_name = name + kAoSuffix;
    SurfaceToRgbaFlipped(
        MakeSurfaceMap(
            s, textures.Has(nrm_name) ? &textures.Get(nrm_name) : nullptr,
            textures.Has(rough_name) ? &textures.Get(rough_name) : nullptr,
            kNormalStrength),
        rgba_bytes);
    StoreTexture(normal_maps_, name, s.w, s.h, rgba_bytes.data());

    const Surface mat = MakeMaterialMap(
        s, textures.Has(metal_name) ? &textures.Get(metal_name) : nullptr,
        textures.Has(ao_name) ? &textures.Get(ao_name) : nullptr);
    if (!mat.Empty()) {
      SurfaceToRgbaFlipped(mat, rgba_bytes);
      StoreTexture(material_maps_, name, mat.w, mat.h, rgba_bytes.data());
    }
  }
  if (default_material_.Id() == 0) {
    // Default material: no metal (R 0), full occlusion value (G 255), i.e.
    // neutral.
    const uint8_t open[4] = {0, 255, 255, 255};
    default_material_.Create();
    default_material_.Allocate(1, 1, open);
    default_material_.SetParams(kNearest, kNearest, kRepeat);
  }
}

void SceneRenderer::SetRenderScale(const int preset) {
  render_scale_ = std::clamp(preset, 0, kRenderScaleCount - 1);
}

void SceneRenderer::ResizeTarget(const int window_w, const int window_h) {
  if (window_w <= 0 || window_h <= 0) return;
  const int h = kRenderScaleLines[render_scale_];
  const int w = std::max(1, static_cast<int>(std::lround(
                                static_cast<double>(h) * window_w / window_h)));
  target_.Resize(w, h);
  bloom_a_.Resize(std::max(1, w / 4), std::max(1, h / 4));
  bloom_b_.Resize(std::max(1, w / 4), std::max(1, h / 4));
  ao_a_.Resize(std::max(1, w / 2), std::max(1, h / 2));
  ao_b_.Resize(std::max(1, w / 2), std::max(1, h / 2));
}

GLuint SceneRenderer::TextureFor(const std::string& name) const {
  for (const auto& [key, texture] : textures_) {
    if (key == name) return texture.Id();
  }
  for (const auto& [key, texture] : textures_) {
    if (key == "missing") return texture.Id();
  }
  return 0;
}

GLuint SceneRenderer::NormalMapFor(const std::string& name) const {
  for (const auto& [key, texture] : normal_maps_) {
    if (key == name) return texture.Id();
  }
  for (const auto& [key, texture] : normal_maps_) {
    if (key == "missing") return texture.Id();
  }
  return 0;
}

GLuint SceneRenderer::MaterialMapFor(const std::string& name) const {
  for (const auto& [key, texture] : material_maps_) {
    if (key == name) return texture.Id();
  }
  return default_material_.Id();
}

void SceneRenderer::UploadBatches(BatchList& dst,
                                  const std::vector<Batch>& src) {
  for (const auto& b : src) {
    if (b.verts.empty()) continue;
    if (b.skin.size() == b.verts.size()) {
      UploadSkinnedBatch(dst, b);
      continue;
    }

    // Group whole triangles by the chunk of their first vertex.
    std::map<std::pair<int, int>, std::vector<Vertex>> chunks;
    for (size_t i = 0; i + 2 < b.verts.size(); i += 3) {
      const Vertex& v = b.verts[i];
      auto& list = chunks[{static_cast<int>(std::floor(v.px / kChunk)),
                           static_cast<int>(std::floor(v.pz / kChunk))}];
      list.insert(list.end(), b.verts.begin() + static_cast<std::ptrdiff_t>(i),
                  b.verts.begin() + static_cast<std::ptrdiff_t>(i) + 3);
    }
    GpuBatch g;
    std::vector<Vertex> all;
    all.reserve(b.verts.size());
    ResetBounds(g.bmin, g.bmax);
    for (const auto& [key, verts] : chunks) {
      GpuChunk c;
      c.first = static_cast<int>(all.size());
      c.count = static_cast<int>(verts.size());
      ResetBounds(c.bmin, c.bmax);
      for (const Vertex& v : verts) {
        const float p[3] = {v.px, v.py, v.pz};
        GrowBounds(c.bmin, c.bmax, p, p);
      }
      GrowBounds(g.bmin, g.bmax, c.bmin, c.bmax);
      all.insert(all.end(), verts.begin(), verts.end());
      g.chunks.push_back(c);
    }
    g.vbo.Upload(all.data(), all.size() * sizeof(Vertex), kStaticDraw);
    g.vao.Create();
    g.vao.Bind();
    SetVertexLayout(g.vbo.Id());
    BindVertexArray(0);
    g.texture = TextureFor(b.texture);
    g.normal = NormalMapFor(b.texture);
    g.material = MaterialMapFor(b.texture);
    g.emissive = b.emissive;
    g.alpha_test = b.alpha_test;
    g.opacity = b.opacity;
    dst.push_back(std::move(g));
  }
}

// A skinned batch is one chunk: its bones move every vertex, so the bounds of
// the bind pose would not hold and the model is never culled per chunk.
void SceneRenderer::UploadSkinnedBatch(BatchList& dst, const Batch& b) {
  GpuBatch g;
  std::vector<SkinnedVertex> all;
  all.reserve(b.verts.size());
  ResetBounds(g.bmin, g.bmax);
  for (size_t i = 0; i < b.verts.size(); ++i) {
    SkinWeights w = b.skin[i];
    for (float& j : w.joints) {
      j = std::clamp(j, 0.0f, static_cast<float>(kMaxBones - 1));
    }
    all.push_back({b.verts[i], w});
  }
  GpuChunk c;
  c.first = 0;
  c.count = static_cast<int>(all.size() / 3 * 3);
  ResetBounds(c.bmin, c.bmax);
  g.bmin[0] = g.bmin[1] = g.bmin[2] = -1e9f;
  g.bmax[0] = g.bmax[1] = g.bmax[2] = 1e9f;
  c.bmin[0] = c.bmin[1] = c.bmin[2] = -1e9f;
  c.bmax[0] = c.bmax[1] = c.bmax[2] = 1e9f;
  g.chunks.push_back(c);
  g.skinned = true;
  g.vbo.Upload(all.data(), all.size() * sizeof(SkinnedVertex), kStaticDraw);
  g.vao.Create();
  g.vao.Bind();
  SetVertexLayout(g.vbo.Id(), true);
  BindVertexArray(0);
  g.texture = TextureFor(b.texture);
  g.normal = NormalMapFor(b.texture);
  g.material = MaterialMapFor(b.texture);
  g.emissive = b.emissive;
  g.alpha_test = b.alpha_test;
  g.opacity = b.opacity;
  dst.push_back(std::move(g));
}

int SceneRenderer::UploadModel(const std::vector<Batch>& batches) {
  ++scene_revision_;
  models_.emplace_back();
  UploadBatches(models_.back(), batches);
  return static_cast<int>(models_.size()) - 1;
}

void SceneRenderer::UploadWorld(const SceneGeometry& geometry) {
  ++scene_revision_;
  world_.clear();
  props_.clear();
  decals_.clear();
  UploadBatches(world_, geometry.world);
  UploadBatches(props_, geometry.props);
  UploadBatches(decals_, geometry.decals);
}

void SceneRenderer::UploadCab(const SceneGeometry& geometry) {
  ++scene_revision_;
  cab_.clear();
  cab_decals_.clear();
  UploadBatches(cab_, geometry.world);
  UploadBatches(cab_, geometry.props);
  UploadBatches(cab_decals_, geometry.decals);
}

void SceneRenderer::UploadDoorPanel(const std::vector<Batch>& batches) {
  ++scene_revision_;
  door_.clear();
  UploadBatches(door_, batches);
}

void SceneRenderer::BindBatchState(const GpuBatch& b) {
  if (b.texture != draw_state_.texture) {
    BindTexture(kTexture2D, b.texture);
    draw_state_.texture = b.texture;
  }
  if (bind_normals_ &&
      (b.normal != draw_state_.normal || b.material != draw_state_.material)) {
    ActiveTexture(kTexture0 + kNormalUnit);
    BindTexture(kTexture2D, b.normal);
    ActiveTexture(kTexture0 + kMaterialUnit);
    BindTexture(kTexture2D, b.material);
    ActiveTexture(kTexture0);
    draw_state_.normal = b.normal;
    draw_state_.material = b.material;
  }
  const int emissive = b.emissive ? 1 : 0, alpha_test = b.alpha_test ? 1 : 0;
  if (emissive != draw_state_.emissive) {
    Uniform1i(u_emissive_, emissive);
    draw_state_.emissive = emissive;
  }
  if (alpha_test != draw_state_.alpha_test) {
    Uniform1i(u_alpha_test_, alpha_test);
    draw_state_.alpha_test = alpha_test;
  }
  if (b.opacity != draw_state_.opacity) {
    Uniform1f(u_opacity_, b.opacity);
    draw_state_.opacity = b.opacity;
  }
  b.vao.Bind();
}

void SceneRenderer::DrawTris(const gl::GLint first, const GLsizei count) {
  DrawArrays(kTriangles, first, count);
  if (counting_shadows_) {
    ++stats_.shadow_draw_calls;
  } else {
    ++stats_.draw_calls;
    stats_.triangles += count / 3;
  }
}

void SceneRenderer::DrawBatches(const BatchList& batches,
                                const float* const model,
                                const Frustum* const cull,
                                const float offset_z) {
  UniformMatrix4fv(u_model_, 1, kFalse, model);
  for (const auto& b : batches) {
    if ((b.opacity < 1.0f) != blend_pass_) continue;  // solid or blended pass
    if (cull && !BoxInFrustum(*cull, b.bmin, b.bmax, offset_z)) continue;
    bool bound = false;

    int run_first = 0, run_count = 0;
    const auto flush = [&] {
      if (run_count > 0) DrawTris(run_first, run_count);
      run_count = 0;
    };
    for (const auto& c : b.chunks) {
      if (cull && !BoxInFrustum(*cull, c.bmin, c.bmax, offset_z)) {
        flush();
        continue;
      }
      if (!bound) {
        bound = true;
        BindBatchState(b);
      }
      if (run_count > 0 && run_first + run_count == c.first) {
        run_count += c.count;
      } else {
        flush();
        run_first = c.first;
        run_count = c.count;
      }
    }
    flush();
  }
}

void SceneRenderer::DrawQuad() {
  quad_vao_.Bind();
  DrawTris(0, 6);
}

void SceneRenderer::DrawSky(const CameraState& cam, const Frame& f) {
  Disable(kDepthTest);
  sky_program_.Use();

  const float cos_pitch = std::cos(static_cast<float>(cam.pitch)),
              sin_pitch = std::sin(static_cast<float>(cam.pitch));
  const float cos_yaw = std::cos(static_cast<float>(cam.yaw)),
              sin_yaw = std::sin(static_cast<float>(cam.yaw));
  sky_program_.Set("uFwd", cos_yaw * cos_pitch, sin_pitch, sin_yaw * cos_pitch);
  sky_program_.Set("uRight", -sin_yaw, 0.0f, cos_yaw);
  sky_program_.Set("uUp", -cos_yaw * sin_pitch, cos_pitch,
                   -sin_yaw * sin_pitch);
  const float tan_half_y = std::tan(f.fov_y * 0.5f);
  sky_program_.Set("uTan", tan_half_y * f.aspect, tan_half_y);
  sky_program_.Set("uTime", static_cast<float>(cam.time));
  sky_program_.Set("uLightning", static_cast<float>(cam.lightning));
  sky_program_.Set("uMoonDir", f.moon[0], f.moon[1], f.moon[2]);
  sky_program_.Set("uFogColor", f.fog[0], f.fog[1], f.fog[2]);
  sky_program_.Set("uZenith", f.sky.zenith[0], f.sky.zenith[1],
                   f.sky.zenith[2]);
  sky_program_.Set("uHorizon", f.sky.horizon[0], f.sky.horizon[1],
                   f.sky.horizon[2]);
  sky_program_.Set("uSunLight", f.sky.light_color[0], f.sky.light_color[1],
                   f.sky.light_color[2]);
  sky_program_.Set("uStars", f.sky.stars);
  sky_program_.Set("uSun", f.sky.sun);
  sky_program_.Set("uCloudLight", f.sky.cloud_light);
  sky_program_.Set("uRes", static_cast<float>(target_.Width()),
                   static_cast<float>(target_.Height()));
  sky_program_.Set("uGrain", f.grain);
  sky_program_.Set("uExposure", f.grade.exposure);
  sky_program_.Set("uTonemap", static_cast<int>(f.grade.tonemap));
  sky_program_.Set("uVignette", f.grade.vignette);
  DrawQuad();
}

void SceneRenderer::SetLampUniforms() {
  const WorldUniforms& u = world_u_;
  const int n = static_cast<int>(lamps_.size());
  float pos[CameraState::kMaxLights * 3], col[CameraState::kMaxLights * 3],
      rad[CameraState::kMaxLights], halo[CameraState::kMaxLights];
  for (int i = 0; i < n; ++i) {
    const PointLight& l = lamps_[static_cast<size_t>(i)];
    const size_t o = static_cast<size_t>(i) * 3;
    pos[o] = static_cast<float>(l.x);
    pos[o + 1] = static_cast<float>(l.y);
    pos[o + 2] = static_cast<float>(l.z);
    col[o] = static_cast<float>(l.r);
    col[o + 1] = static_cast<float>(l.g);
    col[o + 2] = static_cast<float>(l.b);
    rad[i] = static_cast<float>(std::max(0.001, l.radius));
    halo[i] = static_cast<float>(std::clamp(l.halo, 0.0, 1.0));
  }
  Uniform1i(u.light_count, n);
  if (n > 0) {
    Uniform3fv(u.light_pos, n, pos);
    Uniform3fv(u.light_color, n, col);
    Uniform1fv(u.light_radius, n, rad);
    Uniform1fv(u.light_halo, n, halo);
  }
}

void SceneRenderer::BindShadowCubes() {
  for (int i = 0; i < CameraState::kMaxShadowLights; ++i) {
    ActiveTexture(kTexture0 + static_cast<GLenum>(kFirstShadowUnit + i));
    BindTexture(kTextureCubeMap, shadow_[i].Texture());
  }
  ActiveTexture(kTexture0 + kHandShadowUnit);
  BindTexture(kTextureCubeMap, hand_shadow_.Texture());
}

void SceneRenderer::DrawWorld(const CameraState& cam, const Frame& f) {
  Enable(kDepthTest);
  DepthFunc(kLequal);

  world_program_.Use();
  const WorldUniforms& u = world_u_;
  UniformMatrix4fv(u.view, 1, kFalse, f.view);
  UniformMatrix4fv(u.proj, 1, kFalse, f.proj);
  Uniform3f(u.cam, f.eye[0], f.eye[1], f.eye[2]);
  Uniform3f(u.flash_dir, f.flash[0], f.flash[1], f.flash[2]);
  Uniform1f(u.flash_on, cam.flash_on ? 1.0f : 0.0f);
  Uniform1f(u.lighter, cam.lighter ? 1.0f : 0.0f);
  Uniform1f(u.torch_warm,
            static_cast<float>(std::clamp(cam.torch_tint, 0.0, 1.0)));
  Uniform1i(u.outdoor, cam.outdoor ? 1 : 0);
  Uniform3f(u.moon_dir, f.moon[0], f.moon[1], f.moon[2]);
  Uniform3f(u.sun_color, f.sky.light_color[0], f.sky.light_color[1],
            f.sky.light_color[2]);
  Uniform3f(u.sky_ambient, f.sky.ambient[0], f.sky.ambient[1],
            f.sky.ambient[2]);
  Uniform3f(u.fog_color, f.fog[0], f.fog[1], f.fog[2]);
  Uniform1f(u.fog_density, f.fog_density);
  Uniform1f(u.time, static_cast<float>(cam.time));
  Uniform1f(u.mist, static_cast<float>(std::clamp(cam.mist, 0.0, 1.0)));
  Uniform1f(u.mist_height, static_cast<float>(std::max(0.1, cam.mist_height)));
  Uniform3f(u.ambient_base, cam.ambient_base[0], cam.ambient_base[1],
            cam.ambient_base[2]);
  Uniform3f(u.ambient_near, cam.ambient_near[0], cam.ambient_near[1],
            cam.ambient_near[2]);
  Uniform1f(u.lightning, static_cast<float>(cam.lightning));
  {
    float xz[CameraState::kMaxLightningZones * 4] = {};
    float zy[CameraState::kMaxLightningZones * 2] = {};
    const int n = std::min<int>(CameraState::kMaxLightningZones,
                                static_cast<int>(cam.lightning_zones.size()));
    for (int i = 0; i < n; ++i) {
      const LightningZone& z = cam.lightning_zones[static_cast<size_t>(i)];
      xz[i * 4 + 0] = static_cast<float>(z.x0);
      xz[i * 4 + 1] = static_cast<float>(z.z0);
      xz[i * 4 + 2] = static_cast<float>(z.x1);
      xz[i * 4 + 3] = static_cast<float>(z.z1);
      zy[i * 2 + 0] = static_cast<float>(z.y0);
      zy[i * 2 + 1] = static_cast<float>(z.y1);
    }
    Uniform1i(u.zone_count, cam.lightning_zoned ? n : -1);
    if (n > 0) {
      Uniform4fv(u.zone_xz, n, xz);
      Uniform2fv(u.zone_y, n, zy);
    }
  }
  SetLampUniforms();
  Uniform1i(u.shadow_count, shadow_count_);
  Uniform1f(u.shadow_texel, 2.0f / static_cast<float>(shadow_size_));
  Uniform1i(u.hand_shadow, hand_shadow_on_ ? 1 : 0);
  Uniform3f(u.hand_pos, hand_pos_[0], hand_pos_[1], hand_pos_[2]);
  Uniform1i(u.hand_origin, hand_origin_on_ ? 1 : 0);
  Uniform1i(u.hand_faces, static_cast<int>(hand_faces_));
  BindShadowCubes();
  Uniform2f(u.res, static_cast<float>(target_.Width()),
            static_cast<float>(target_.Height()));
  Uniform1f(u.grain, f.grain);
  Uniform1f(u.exposure, f.grade.exposure);
  Uniform1i(u.tonemap, static_cast<int>(f.grade.tonemap));
  Uniform1f(u.vignette, f.grade.vignette);
  Uniform1i(u.materials, f.materials);
  Uniform4f(u.clip, f.clip[0], f.clip[1], f.clip[2], f.clip[3]);
  ActiveTexture(kTexture0);
  draw_state_ = DrawState{};

  DrawScene(cam, f.planes, f.views);
}

void SceneRenderer::DrawScene(const CameraState& cam, const Frustum& cull,
                              const unsigned views) {
  const auto draw_all = [&] {
    float model[16];
    Mat4Identity(model);
    DrawBatches(cab_, model, &cull);
    model[14] = static_cast<float>(cam.scroll_z);
    DrawBatches(world_, model, &cull, static_cast<float>(cam.scroll_z));
    DrawBatches(props_, model, &cull, static_cast<float>(cam.scroll_z));
    DrawDoors(cam, cull, model);
    if (views != 0) DrawInstances(cam, views);
  };
  blend_pass_ = false;
  draw_all();
  if (bind_normals_ && (!decals_.empty() || !cab_decals_.empty())) {
    Enable(kBlend);
    BlendFunc(kSrcAlpha, kOneMinusSrcAlpha);
    DepthMask(kFalse);
    Enable(gl::kPolygonOffsetFill);
    gl::PolygonOffset(-1.0f, -1.0f);
    Uniform1i(u_decal_, 1);
    float model[16];
    Mat4Identity(model);
    DrawBatches(cab_decals_, model, &cull);
    model[14] = static_cast<float>(cam.scroll_z);
    DrawBatches(decals_, model, &cull, static_cast<float>(cam.scroll_z));
    Uniform1i(u_decal_, 0);
    Disable(gl::kPolygonOffsetFill);
    DepthMask(kTrue);
    Disable(kBlend);
  }
  // Then everything blended (glass, dust, mist cards), over the solid scene.
  // Shadow passes (no normal maps bound) draw solids only.
  if (bind_normals_) {
    blend_pass_ = true;
    Enable(kBlend);
    BlendFunc(kSrcAlpha, kOneMinusSrcAlpha);
    draw_all();
    Disable(kBlend);
    blend_pass_ = false;
  }
}

void SceneRenderer::DrawDoors(const CameraState& cam, const Frustum& cull,
                              float* const model) {
  for (const auto& door : cam.doors) {
    const auto hinge_x = static_cast<float>(door.hinge_x),
               hinge_z = static_cast<float>(door.hinge_z);
    const float reach = kDoorPanelWidth + kDoorCullMargin;
    const auto hinge_y = static_cast<float>(door.y);
    const float bmin[3] = {hinge_x - reach, hinge_y, hinge_z - reach};
    const float bmax[3] = {hinge_x + reach,
                           hinge_y + kDoorPanelHeight + kDoorCullMargin,
                           hinge_z + reach};
    if (!BoxInFrustum(cull, bmin, bmax)) continue;
    DoorModel(door.hinge_x, door.y, door.hinge_z, door.angle, model);
    DrawBatches(door_, model);
  }
}

void SceneRenderer::DrawInstances(const CameraState& cam,
                                  const unsigned views) {
  // Plan the frame's instances: those of these views, culled and swapped for
  // a cheaper model by distance from the eye (never for shadow casters, which
  // must not pop in and out of the shadow maps), sorted by model.
  const bool lod = (views & ModelInstance::kShadow) == 0;
  const double ex = cam.x, ey = cam.eye_height, ez = cam.z;
  lod_requests_.clear();
  lod_instances_.clear();
  for (const ModelInstance& inst : cam.instances) {
    if ((inst.views & views) == 0 || inst.model < 0 ||
        std::cmp_greater_equal(inst.model, models_.size())) {
      continue;
    }
    LodRequest r;
    r.model = inst.model;
    if (lod) {
      const double dx = inst.matrix[12] - ex, dy = inst.matrix[13] - ey,
                   dz = inst.matrix[14] - ez;
      r.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
      r.max_distance = inst.max_distance;
    }
    lod_requests_.push_back(r);
    lod_instances_.push_back(&inst);
  }
  for (const LodDraw& draw : PlanDraws(lod_requests_, lod ? lods_ : no_lods_)) {
    const ModelInstance& inst = *lod_instances_[draw.request];
    if (std::cmp_greater_equal(draw.model, models_.size())) continue;
    // The bone matrices (at most kMaxBones) go up once per instance; static
    // batches of the same model ignore them.
    const bool skinned = !inst.skin.empty();
    if (skinned) {
      const auto count = static_cast<GLsizei>(
          std::min(inst.skin.size(), static_cast<size_t>(kMaxBones)));
      Uniform1i(u_skinned_, 1);
      UniformMatrix4fv(u_bones_, count, kFalse, inst.skin[0].data());
    }
    DrawBatches(models_[static_cast<size_t>(draw.model)], inst.matrix);
    if (skinned) Uniform1i(u_skinned_, 0);
  }
}

bool SceneRenderer::MirrorVisible(const CameraState& cam,
                                  const Frame& f) const {
  const MirrorState& m = cam.mirror;
  if (!m.on || !mirror_program_.Valid() || !mirror_target_.Width()) {
    return false;
  }
  const double front = (cam.x - m.x) * m.nx + (cam.z - m.z) * m.nz;
  if (front < kMirrorMinFront) return false;
  const double radius = std::hypot(m.half_w, m.half_h);
  const float bmin[3] = {static_cast<float>(m.x - radius),
                         static_cast<float>(m.y - radius),
                         static_cast<float>(m.z - radius)};
  const float bmax[3] = {static_cast<float>(m.x + radius),
                         static_cast<float>(m.y + radius),
                         static_cast<float>(m.z + radius)};
  return BoxInFrustum(f.planes, bmin, bmax);
}

void SceneRenderer::DrawMirrorView(const CameraState& cam, const Frame& f) {
  const MirrorState& m = cam.mirror;
  const int h = std::max(kMirrorMinSide,
                         target_.Height() * kMirrorScaleNum / kMirrorScaleDen);
  const int w = std::max(kMirrorMinSide,
                         target_.Width() * kMirrorScaleNum / kMirrorScaleDen);
  mirror_target_.Resize(w, h);
  const float n[3] = {static_cast<float>(m.nx), 0.0f, static_cast<float>(m.nz)};
  const float d =
      n[0] * static_cast<float>(m.x) + n[2] * static_cast<float>(m.z);
  float reflect[16];
  Mat4Identity(reflect);
  reflect[0] = 1 - 2 * n[0] * n[0];
  reflect[2] = -2 * n[0] * n[2];
  reflect[8] = -2 * n[2] * n[0];
  reflect[10] = 1 - 2 * n[2] * n[2];
  reflect[12] = 2 * d * n[0];
  reflect[14] = 2 * d * n[2];
  Frame fm = f;
  // Reflect the world about the glass plane (a mirror about a vertical plane
  // with normal n and offset d); lighting stays unreflected.
  Mat4Multiply(f.view, reflect, fm.view);
  float view_proj[16];
  Mat4Multiply(f.proj, fm.view, view_proj);
  FrustumPlanes(view_proj, fm.planes);

  // Clip everything behind the glass plane.
  fm.clip[0] = n[0];
  fm.clip[1] = 0.0f;
  fm.clip[2] = n[2];
  fm.clip[3] = -d - kMirrorClipOffset;
  fm.views = ModelInstance::kMirror;
  mirror_target_.Bind();
  Viewport(0, 0, w, h);
  ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  Clear(kColorBufferBit | kDepthBufferBit);
  Disable(kBlend);
  DrawWorld(cam, fm);
}

void SceneRenderer::DrawMirrorGlass(const CameraState& cam, const Frame& f) {
  const MirrorState& m = cam.mirror;
  const auto nx = static_cast<float>(m.nx), nz = static_cast<float>(m.nz);
  const float along_x = nz, along_z = -nx;
  const float center_x = static_cast<float>(m.x) + nx * kMirrorGlassOffset;
  const float center_z = static_cast<float>(m.z) + nz * kMirrorGlassOffset;
  const auto half_w = static_cast<float>(m.half_w),
             half_h = static_cast<float>(m.half_h);
  const auto center_y = static_cast<float>(m.y);
  const auto corner = [&](const float sx, const float sy) {
    Vertex v{};
    v.px = center_x + along_x * half_w * sx;
    v.py = center_y + half_h * sy;
    v.pz = center_z + along_z * half_w * sx;
    v.nx = nx;
    v.nz = nz;
    return v;
  };
  const Vertex quad[6] = {corner(-1, -1), corner(1, -1), corner(1, 1),
                          corner(-1, -1), corner(1, 1),  corner(-1, 1)};
  mirror_vbo_.Upload(quad, sizeof(quad), kDynamicDraw);
  mirror_program_.Use();
  UniformMatrix4fv(mirror_program_.Uniform("uView"), 1, kFalse, f.view);
  UniformMatrix4fv(mirror_program_.Uniform("uProj"), 1, kFalse, f.proj);
  mirror_program_.Set("uTex", 0);
  mirror_program_.Set("uRes", static_cast<float>(target_.Width()),
                      static_cast<float>(target_.Height()));
  mirror_program_.Set("uTint", static_cast<float>(m.tint[0]),
                      static_cast<float>(m.tint[1]),
                      static_cast<float>(m.tint[2]));
  mirror_program_.Set("uAlpha", static_cast<float>(m.alpha));
  {
    // The glare of the hand light: from the torch's tip when the game says
    // where it is, else from where the hand rests.
    float origin[3] = {hand_pos_[0], hand_pos_[1], hand_pos_[2]};
    if (!hand_origin_on_) {
      const float rx = -std::sin(static_cast<float>(cam.yaw)),
                  rz = std::cos(static_cast<float>(cam.yaw));
      const float side = 0.14f + static_cast<float>(cam.hand_side);
      origin[0] = f.eye[0] + rx * side + f.dir[0] * 0.05f;
      origin[1] = f.eye[1] - 0.20f - static_cast<float>(cam.hand_down);
      origin[2] = f.eye[2] + rz * side + f.dir[2] * 0.05f;
    }
    const float on =
        cam.flash_on ? static_cast<float>(std::max(0.0, m.flare)) : 0.0f;
    mirror_program_.Set("uFlare", on);
    mirror_program_.Set("uFlareOrigin", origin[0], origin[1], origin[2]);
    mirror_program_.Set("uFlareDir", f.flash[0], f.flash[1], f.flash[2]);
    if (cam.lighter)
      mirror_program_.Set("uFlareColor", 1.0f, 0.62f, 0.30f);
    else
      mirror_program_.Set("uFlareColor", 1.0f, 0.86f, 0.62f);
    mirror_program_.Set("uEye", f.eye[0], f.eye[1], f.eye[2]);
    mirror_program_.Set("uGlassCenter", center_x, center_y, center_z);
    mirror_program_.Set("uGlassNormal", nx, 0.0f, nz);
    mirror_program_.Set("uGlassAlong", along_x, 0.0f, along_z);
  }
  ActiveTexture(kTexture0);
  mirror_target_.Color().Bind();
  Enable(kDepthTest);
  DepthFunc(kLequal);
  Enable(kBlend);
  BlendFunc(kSrcAlpha, kOneMinusSrcAlpha);
  mirror_vao_.Bind();
  DrawTris(0, 6);
  Disable(kBlend);
}

void SceneRenderer::DrawParticles(const CameraState& cam, const Frame& f) {
  if (cam.particles.empty() || !particle_program_.Valid()) return;
  const size_t count = std::min(
      cam.particles.size(), static_cast<size_t>(CameraState::kMaxParticles));
  // The camera's right and up vectors, so every sprite faces the eye.
  const float dx = f.dir[0], dy = f.dir[1], dz = f.dir[2];
  const float right_len = std::max(1e-4f, std::hypot(dx, dz));
  const float rx = -dz / right_len, rz = dx / right_len;
  const float ux = -rz * dy, uy = rz * dx - rx * dz, uz = rx * dy;
  const auto scroll = static_cast<float>(cam.scroll_z);

  // Translucent sprites far to near, then the additive ones (their order does
  // not matter).
  std::vector<std::pair<float, size_t>> order;
  std::vector<size_t> additive;
  for (size_t i = 0; i < count; ++i) {
    const ParticleSprite& s = cam.particles[i];
    if (!std::isfinite(s.x + s.y + s.z + s.size + s.rotation) ||
        s.size <= 0.0f) {
      continue;
    }
    if (s.blend == ParticleBlend::kAdditive) {
      additive.push_back(i);
      continue;
    }
    const float ex = s.x - f.eye[0], ey = s.y - f.eye[1],
                ez = s.z + scroll - f.eye[2];
    order.emplace_back(ex * ex + ey * ey + ez * ez, i);
  }
  std::ranges::sort(order, std::greater<>());
  const auto translucent = static_cast<GLsizei>(order.size()) * 6;
  const auto glow = static_cast<GLsizei>(additive.size()) * 6;
  if (translucent + glow == 0) return;
  for (const size_t i : additive) order.emplace_back(0.0f, i);

  particle_verts_.clear();
  particle_verts_.reserve(order.size() * 6 * kParticleFloats);
  constexpr float kCorner[6][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f},
                                   {0.5f, 0.5f},   {-0.5f, -0.5f},
                                   {0.5f, 0.5f},   {-0.5f, 0.5f}};
  for (const auto& entry : order) {
    const ParticleSprite& s = cam.particles[entry.second];
    const float c = std::cos(s.rotation), sn = std::sin(s.rotation);
    for (const auto& corner : kCorner) {
      const float px = (corner[0] * c - corner[1] * sn) * s.size;
      const float py = (corner[0] * sn + corner[1] * c) * s.size;
      const float v[kParticleFloats] = {
          s.x + rx * px + ux * py,
          s.y + uy * py,
          s.z + scroll + rz * px + uz * py,
          corner[0] + 0.5f,
          corner[1] + 0.5f,
          s.color[0],
          s.color[1],
          s.color[2],
          s.color[3],
          static_cast<float>(static_cast<int>(s.shape))};
      particle_verts_.insert(particle_verts_.end(), v, v + kParticleFloats);
    }
  }
  stats_.particles += static_cast<int>(order.size());
  particle_vbo_.Upload(particle_verts_.data(),
                       particle_verts_.size() * sizeof(float), kDynamicDraw);

  particle_program_.Use();
  UniformMatrix4fv(particle_program_.Uniform("uView"), 1, kFalse, f.view);
  UniformMatrix4fv(particle_program_.Uniform("uProj"), 1, kFalse, f.proj);
  Enable(kDepthTest);
  DepthFunc(kLequal);
  DepthMask(
      kFalse);  // sprites test against the scene but never hide each other
  Enable(kBlend);
  particle_vao_.Bind();
  if (translucent > 0) {
    BlendFunc(kSrcAlpha, kOneMinusSrcAlpha);
    DrawTris(0, translucent);
  }
  if (glow > 0) {
    BlendFunc(kSrcAlpha, kOne);
    DrawTris(translucent, glow);
  }
  Disable(kBlend);
  DepthMask(kTrue);
}

void SceneRenderer::DrawGlassPanes(const CameraState& cam, const Frame& f) {
  if (cam.glass.empty() || !glass_program_.Valid()) return;
  glass_program_.Use();
  UniformMatrix4fv(glass_program_.Uniform("uView"), 1, kFalse, f.view);
  UniformMatrix4fv(glass_program_.Uniform("uProj"), 1, kFalse, f.proj);
  Enable(kDepthTest);
  DepthFunc(kLequal);
  Enable(kBlend);
  BlendFunc(kSrcAlpha, kOneMinusSrcAlpha);
  glass_vao_.Bind();
  for (const GlassPane& pane : cam.glass) {
    const auto corner = [&](const double x, const double z, const double y) {
      Vertex v{};
      v.px = static_cast<float>(x);
      v.py = static_cast<float>(y);
      v.pz = static_cast<float>(z);
      v.u = static_cast<float>(std::hypot(x - pane.x0, z - pane.z0));  // along
      v.v = static_cast<float>(y);                                     // up
      return v;
    };
    const Vertex a = corner(pane.x0, pane.z0, pane.y0),
                 b = corner(pane.x1, pane.z1, pane.y0),
                 c = corner(pane.x1, pane.z1, pane.y1),
                 d = corner(pane.x0, pane.z0, pane.y1);
    const Vertex quad[6] = {a, b, c, a, c, d};
    glass_vbo_.Upload(quad, sizeof(quad), kDynamicDraw);
    glass_program_.Set("uTint", pane.tint[0], pane.tint[1], pane.tint[2]);
    glass_program_.Set("uAlpha", pane.alpha);
    glass_program_.Set("uDrops", pane.drops);
    glass_program_.Set("uTime", static_cast<float>(cam.time));
    glass_program_.Set("uEye", f.eye[0], f.eye[1], f.eye[2]);
    DrawTris(0, 6);
  }
  Disable(kBlend);
}

void SceneRenderer::MotionBlurSmear(const CameraState& cam, const Frame& f,
                                    float& smear_x, float& smear_y) {
  smear_x = smear_y = 0.0f;
  if (cam.motion_blur > 0.0 && have_last_view_) {
    double dyaw = cam.yaw - last_yaw_;
    dyaw = std::remainder(dyaw, kTwoPi);
    const double dpitch = cam.pitch - last_pitch_;
    const double fov_x = 2.0 * std::atan(std::tan(f.fov_y * 0.5) * f.aspect);

    const double cap = kMotionBlurCap * std::max(1.0, cam.motion_blur);
    smear_x = static_cast<float>(
        std::clamp(dyaw / fov_x * cam.motion_blur, -cap, cap));
    smear_y = static_cast<float>(
        std::clamp(dpitch / f.fov_y * cam.motion_blur, -cap, cap));
  }
  last_yaw_ = cam.yaw;
  last_pitch_ = cam.pitch;
  have_last_view_ = true;
}

// The occlusion pass: reads the resolved depth of the scene, writes the
// occlusion at half size, then blurs it (the bloom blur, no threshold) to hide
// the per-pixel rotation of the sample ring. Returns whether `ao_a_` holds it.
bool SceneRenderer::BuildAo(const CameraState& cam, const Frame& f) {
  if (cam.ssao <= 0.0 || !ssao_program_.Valid() || !bloom_program_.Valid() ||
      !ao_a_.Width()) {
    return false;
  }
  Disable(kDepthTest);
  Disable(kBlend);
  ao_a_.Bind();
  Viewport(0, 0, ao_a_.Width(), ao_a_.Height());
  ssao_program_.Use();
  ActiveTexture(kTexture0);
  target_.DepthTexture().Bind();
  ssao_program_.Set("uDepth", 0);
  const float tan_half_y = std::tan(f.fov_y * 0.5f);
  ssao_program_.Set("uTan", tan_half_y * f.aspect, tan_half_y);
  ssao_program_.Set("uNearFar", kNearPlane, kFarPlane);
  ssao_program_.Set("uTexel", 1.0f / static_cast<float>(target_.Width()),
                    1.0f / static_cast<float>(target_.Height()));
  ssao_program_.Set("uRadius", 0.7f);
  DrawQuad();

  bloom_program_.Use();
  bloom_program_.Set("uTex", 0);
  bloom_program_.Set("uThreshold", 0.0f);
  const float tx = 1.0f / static_cast<float>(ao_a_.Width());
  const float ty = 1.0f / static_cast<float>(ao_a_.Height());
  struct Pass {
    const RenderTarget* from;
    RenderTarget* to;
    float dx, dy;
  };
  const Pass passes[2] = {{&ao_a_, &ao_b_, tx, 0.0f},
                          {&ao_b_, &ao_a_, 0.0f, ty}};
  for (const Pass& p : passes) {
    p.to->Bind();
    Viewport(0, 0, p.to->Width(), p.to->Height());
    p.from->Color().Bind();
    bloom_program_.Set("uDir", p.dx, p.dy);
    DrawQuad();
  }
  return true;
}

bool SceneRenderer::BuildBloom(const CameraState& cam) {
  if (cam.bloom <= 0.0 || !bloom_program_.Valid() || !bloom_a_.Width())
    return false;
  Disable(kDepthTest);
  Disable(kBlend);
  bloom_program_.Use();
  bloom_program_.Set("uTex", 0);
  ActiveTexture(kTexture0);
  const float tx = 1.0f / static_cast<float>(bloom_a_.Width());
  const float ty = 1.0f / static_cast<float>(bloom_a_.Height());

  struct Pass {
    const RenderTarget* from;
    RenderTarget* to;
    float dx, dy, threshold;
  };
  // Bright pass with a first horizontal blur, then vertical and a wider
  // horizontal and vertical pair; only the first pass applies the threshold.
  const Pass passes[4] = {{&target_, &bloom_a_, tx * 1.5f, 0.0f, 0.40f},
                          {&bloom_a_, &bloom_b_, 0.0f, ty * 1.5f, 0.0f},
                          {&bloom_b_, &bloom_a_, tx * 2.5f, 0.0f, 0.0f},
                          {&bloom_a_, &bloom_b_, 0.0f, ty * 2.5f, 0.0f}};
  for (const Pass& p : passes) {
    p.to->Bind();
    Viewport(0, 0, p.to->Width(), p.to->Height());
    p.from->Color().Bind();
    bloom_program_.Set("uDir", p.dx, p.dy);
    bloom_program_.Set("uThreshold", p.threshold);
    DrawQuad();
  }
  return true;
}

void SceneRenderer::Composite(const CameraState& cam, const Frame& f,
                              const int window_w, const int window_h) {
  RenderTarget::BindWindow();
  Viewport(0, 0, window_w, window_h);
  ClearColor(0, 0, 0, 1);
  Clear(kColorBufferBit | kDepthBufferBit);
  Disable(kDepthTest);
  Disable(kBlend);

  float bx = 0.0f, by = 0.0f;
  MotionBlurSmear(cam, f, bx, by);

  const bool ao = BuildAo(cam, f);
  const bool bloom = BuildBloom(cam);
  RenderTarget::BindWindow();
  Viewport(0, 0, window_w, window_h);
  composite_program_.Use();
  ActiveTexture(kTexture0 + 1);
  bloom_b_.Color().Bind();
  ActiveTexture(kTexture0);
  target_.Color().Bind();
  composite_program_.Set("uTex", 0);
  composite_program_.Set("uBloomTex", 1);
  composite_program_.Set("uBloom",
                         bloom ? static_cast<float>(cam.bloom) : 0.0f);
  composite_program_.Set("uBlur", bx, by);
  composite_program_.Set("uLens", cam.lens_effect ? 1.0f : 0.0f);
  const ColorGrade& grade = f.grade;
  const bool lut = lut_size_ > 0 && grade.lut_amount > 0.0f;
  composite_program_.Set("uGradeOn",
                         !grade.PostIsIdentity() || lut ? 1.0f : 0.0f);
  composite_program_.Set("uContrast", grade.contrast);
  composite_program_.Set("uSaturation", grade.saturation);
  composite_program_.Set("uTint", grade.tint[0], grade.tint[1], grade.tint[2]);
  composite_program_.Set("uLutSize",
                         lut ? static_cast<float>(lut_size_) : 0.0f);
  composite_program_.Set("uLutAmount", grade.lut_amount);
  if (lut) {
    ActiveTexture(kTexture0 + 2);
    lut_tex_.Bind();
    composite_program_.Set("uLutTex", 2);
    ActiveTexture(kTexture0);
  }
  composite_program_.Set(
      "uAo", ao ? static_cast<float>(std::clamp(cam.ssao, 0.0, 1.0)) : 0.0f);
  if (ao) {
    ActiveTexture(kTexture0 + 3);
    ao_a_.Color().Bind();
    composite_program_.Set("uAoTex", 3);
    ActiveTexture(kTexture0);
  }
  composite_program_.Set("uTape", cam.tape ? 1.0f : 0.0f);
  composite_program_.Set("uTapeTime",
                         static_cast<float>(std::max(0.0, cam.tape_time)));
  composite_program_.Set(
      "uAspect",
      static_cast<float>(window_w) / static_cast<float>(std::max(1, window_h)));

  composite_program_.Set(
      "uFxaa", cam.anti_alias == 1 && bx == 0.0f && by == 0.0f ? 1.0f : 0.0f);
  composite_program_.Set("uTexel", 1.0f / static_cast<float>(target_.Width()),
                         1.0f / static_cast<float>(target_.Height()));
  DrawQuad();
}

bool SceneRenderer::SetColorLut(const Surface& strip) {
  const int size = LutStripSize(strip);
  lut_size_ = 0;
  if (size == 0) return false;
  if (!lut_tex_.Id()) lut_tex_.Create();
  lut_tex_.SetParams(kLinear, kLinear, kClampToEdge);
  // The strip is 0xAARRGGBB, laid out as the overlay's is.
  lut_tex_.Bind();
  TexImage2D(kTexture2D, 0, kRgba8, strip.w, strip.h, 0, kBgra,
             kUnsignedInt8888Rev, strip.px.data());
  lut_size_ = size;
  return true;
}

void SceneRenderer::UploadOverlay(const Surface& overlay) {
  overlay_tex_.Bind();
  const bool same_size = overlay.w == overlay_w_ && overlay.h == overlay_h_;
  if (same_size && overlay.px == overlay_pixels_) return;
  if (same_size) {
    TexSubImage2D(kTexture2D, 0, 0, 0, overlay.w, overlay.h, kBgra,
                  kUnsignedInt8888Rev, overlay.px.data());
  } else {
    TexImage2D(kTexture2D, 0, kRgba8, overlay.w, overlay.h, 0, kBgra,
               kUnsignedInt8888Rev, overlay.px.data());
    overlay_w_ = overlay.w;
    overlay_h_ = overlay.h;
  }
  overlay_pixels_ = overlay.px;
}

void SceneRenderer::DrawOverlay(const Surface& overlay,
                                const TextLayer* const text) {
  UploadOverlay(overlay);
  quad_program_.Use();
  quad_program_.Set("uFlipY", 1.0f);
  ActiveTexture(kTexture0);
  overlay_tex_.Bind();
  Enable(kBlend);
  BlendFunc(kSrcAlpha, kOneMinusSrcAlpha);
  DrawQuad();
  Disable(kBlend);
  quad_program_.Set("uFlipY", 0.0f);
  if (text && !text->runs.empty()) text_.Draw(*text, overlay.w, overlay.h);
}

SceneRenderer::Frame SceneRenderer::MakeFrame(const CameraState& cam) const {
  Frame f;
  f.eye[0] = static_cast<float>(cam.x);
  f.eye[1] = static_cast<float>(cam.eye_height);
  f.eye[2] = static_cast<float>(cam.z);
  f.dir[0] = static_cast<float>(std::cos(cam.yaw) * std::cos(cam.pitch));
  f.dir[1] = static_cast<float>(std::sin(cam.pitch));
  f.dir[2] = static_cast<float>(std::sin(cam.yaw) * std::cos(cam.pitch));
  {
    const double flash_yaw = cam.yaw + cam.hand_yaw;
    const double flash_pitch =
        std::clamp(cam.pitch + cam.hand_pitch, -kMaxHandPitch, kMaxHandPitch);
    f.flash[0] =
        static_cast<float>(std::cos(flash_yaw) * std::cos(flash_pitch));
    f.flash[1] = static_cast<float>(std::sin(flash_pitch));
    f.flash[2] =
        static_cast<float>(std::sin(flash_yaw) * std::cos(flash_pitch));
  }
  f.sky = cam.outdoor && cam.time_of_day >= 0 && std::isfinite(cam.time_of_day)
              ? SkyAt(cam.time_of_day)
              : NightLook();
  std::ranges::copy(f.sky.light_dir, f.moon);

  f.fog[0] = cam.outdoor ? f.sky.fog[0] : 0.006f;
  f.fog[1] = cam.outdoor ? f.sky.fog[1] : 0.005f;
  f.fog[2] = cam.outdoor ? f.sky.fog[2] : 0.005f;
  f.fog_density = cam.outdoor ? kOutdoorFogDensity : kRoomFogDensity;
  f.fov_y = kFovY * static_cast<float>(std::isfinite(cam.fov_scale)
                                           ? Clampd(cam.fov_scale, 0.5, 1.5)
                                           : 1.0);
  f.views = ModelInstance::kMain;
  f.aspect = static_cast<float>(target_.Width()) /
             static_cast<float>(target_.Height());
  f.grain = cam.grain ? 1.0f : 0.0f;
  f.grade = Sanitize(cam.grade);
  f.materials = std::clamp(cam.materials, 0, 2);
  const float center[3] = {f.eye[0] + f.dir[0], f.eye[1] + f.dir[1],
                           f.eye[2] + f.dir[2]};
  const float up[3] = {0, 1, 0};
  Mat4LookAt(f.eye, center, up, f.view);
  Mat4Perspective(f.fov_y, f.aspect, kNearPlane, kFarPlane, f.proj);
  float view_proj[16];
  Mat4Multiply(f.proj, f.view, view_proj);
  FrustumPlanes(view_proj, f.planes);
  return f;
}

void SceneRenderer::DrawMainView(const CameraState& cam, const Frame& f,
                                 const bool mirrored) {
  target_.Bind();
  Viewport(0, 0, target_.Width(), target_.Height());
  ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  Clear(kColorBufferBit | kDepthBufferBit);
  Disable(kBlend);
  if (cam.outdoor) DrawSky(cam, f);
  DrawWorld(cam, f);
  DrawParticles(cam, f);
  DrawGlassPanes(cam, f);
  if (mirrored) DrawMirrorGlass(cam, f);
  target_.Resolve();
}

void SceneRenderer::Render(const CameraState& cam, const int window_w,
                           const int window_h, const Surface* const overlay,
                           const TextLayer* const text) {
  target_.SetSamples(
      kAntiAliasSamples[std::clamp(cam.anti_alias, 0, kAntiAliasCount - 1)]);
  ResizeTarget(window_w, window_h);
  const Frame f = MakeFrame(cam);

  stats_ = RenderStats{};
  ChooseLamps(cam, f);
  stats_.lights = static_cast<int>(lamps_.size());
  stats_.shadow_casters = shadow_count_ + (hand_shadow_on_ ? 1 : 0);
  DrawShadows(cam);
  const bool mirrored = MirrorVisible(cam, f);
  stats_.mirror = mirrored;
  if (mirrored) {
    DrawMirrorView(cam, f);
  }
  target_.SetResolveDepth(cam.ssao > 0.0);
  DrawMainView(cam, f, mirrored);

  Composite(cam, f, window_w, window_h);
  if (overlay && !overlay->Empty()) DrawOverlay(*overlay, text);
}

DoorInstance MakeDoorInstance(const DoorState& d) {
  const double eased = d.open * d.open * (3.0 - 2.0 * d.open);
  const double swing = kHalfPi * eased;
  const double along = kHingeBase + kHingeSwingShift * d.swing;
  struct DoorInstance di;
  if (d.passage_along_x) {
    di.hinge_x = d.x + along;
    di.hinge_z = d.y + kHingeAcross;
    di.angle = kHalfPi - d.swing * swing;
  } else {
    di.hinge_x = d.x + kHingeAcross;
    di.hinge_z = d.y + along;
    di.angle = d.swing * swing;
  }
  return di;
}

void ApplyCameraFx(const CameraFxOffsets& fx, CameraState& cam) {
  cam.yaw += fx.yaw;
  cam.pitch += fx.pitch;
  // The view's right vector is (-sin yaw, cos yaw) on the x, z plane.
  cam.x += -std::sin(cam.yaw) * fx.right;
  cam.z += std::cos(cam.yaw) * fx.right;
  cam.eye_height += fx.up;
  cam.fov_scale *= fx.fov_scale;
}

void ApplyFreeCam(const FreeCamera& free_cam, CameraState& cam) {
  cam.x = free_cam.x;
  cam.z = free_cam.z;
  cam.eye_height = free_cam.y;
  cam.yaw = free_cam.yaw;
  cam.pitch = free_cam.pitch;
}
}
