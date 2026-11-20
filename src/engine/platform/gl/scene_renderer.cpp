#include "engine/platform/gl/scene_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>

#include "engine/core/mat4.h"
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

// Door panel hinged at (hingeX, hingeZ), its spine turned to absolute `angle`
// on the map plane.
void DoorModel(double hinge_x, double hinge_y, double hinge_z, double angle,
               float* m) {
  const float c = static_cast<float>(std::cos(angle)),
              s = static_cast<float>(std::sin(angle));
  Mat4Identity(m);
  m[0] = c;
  m[2] = s;  // local +x (the spine) -> (cos a, 0, sin a)
  m[8] = -s;
  m[10] = c;  // local +z (the thickness) stays perpendicular
  m[12] = static_cast<float>(hinge_x);
  m[13] = static_cast<float>(hinge_y);
  m[14] = static_cast<float>(hinge_z);
}

constexpr float kFovY = 62.0f * std::numbers::pi_v<float> / 180.0f;
constexpr float
    kNearPlane = 0.05f,
    kFarPlane = 80.0f;  // metres; far is past the longest sightline in a house
constexpr double kTwoPi = 6.283185307179586;
constexpr double kHalfPi = 1.5707963;
constexpr float kBoundsEmpty =
    1e30f;  // starting value of a min/max box (anything shrinks it)
constexpr float kRoomFogDensity = 0.075f,
                kOutdoorFogDensity = 0.040f;  // outdoors the air is clearer
constexpr double kMaxHandPitch = 1.5;  // radians: the lagging hand light never
                                       // points quite straight up or down

// How far past the panel's own size the door's culling box reaches, in metres.
constexpr float kDoorCullMargin = 0.1f;

// Door hinge placement within its cell (see doorInstance): kHingeAcross is the
// hinge's offset on the axis across the passage; the other axis is kHingeBase,
// shifted by the swing side (-1 or 1).
constexpr double kHingeAcross = 0.175, kHingeBase = 0.5, kHingeSwingShift = 0.2;

// Mirror tuning (metres unless noted).
constexpr double kMirrorMinFront =
    0.02;  // the eye must be this far in front of the glass to see it
constexpr float kMirrorClipOffset =
    0.015f;  // the reflection keeps what is this far behind the plane, so the
             // frame's own face drops out
constexpr float kMirrorGlassOffset =
    0.004f;  // the glass quad floats this far off the prop's face (no z-fight)
constexpr int kMirrorMinSide =
    16;  // texels; the reflection target never shrinks below this
// The reflection is drawn at three quarters of the main target's size.
constexpr int kMirrorScaleNum = 3, kMirrorScaleDen = 4;

// Motion blur: the smear is capped at this fraction of the picture (more for
// stronger settings).
constexpr double kMotionBlurCap = 0.08;

// The moon hangs over the house to the north, low enough to sit in view on the
// walk to the front door; the moonlight in the world shader comes from the same
// direction.
constexpr float kMoonDir[3] = {0.148f, 0.417f, -0.897f};

constexpr float kNormalStrength =
    2.5f;  // how far a texture's brightness tilts its surface
constexpr float kChunk = 8.0f;  // cells per side of a culling chunk
constexpr GLsizei kVertexStride =
    32;  // bytes: 3 floats position, 3 normal, 2 uv
constexpr int kNormalUnit = 6,
              kMaterialUnit =
                  7;  // texture units of a batch's normal and material maps
constexpr int kFirstShadowUnit = 1,
              kHandShadowUnit =
                  5;  // the lamps' cubes on 1..4, the hand light's on 5

// Binds the interleaved position / normal / uv layout of a Vertex to the bound
// VAO.
void SetVertexLayout(GLuint vbo) {
  BindBuffer(kArrayBuffer, vbo);
  EnableVertexAttribArray(0);
  VertexAttribPointer(0, 3, kFloat, kFalse, kVertexStride, nullptr);
  EnableVertexAttribArray(1);
  VertexAttribPointer(1, 3, kFloat, kFalse, kVertexStride,
                      reinterpret_cast<const void*>(12));
  EnableVertexAttribArray(2);
  VertexAttribPointer(2, 2, kFloat, kFalse, kVertexStride,
                      reinterpret_cast<const void*>(24));
}

void ResetBounds(float* bmin, float* bmax) {
  for (int k = 0; k < 3; ++k) {
    bmin[k] = kBoundsEmpty;
    bmax[k] = -kBoundsEmpty;
  }
}

void GrowBounds(float* bmin, float* bmax, const float* other_min,
                const float* other_max) {
  for (int k = 0; k < 3; ++k) {
    bmin[k] = std::min(bmin[k], other_min[k]);
    bmax[k] = std::max(bmax[k], other_max[k]);
  }
}

}  // namespace

bool SceneRenderer::Init(const TextureLibrary& textures) {
  // World and sky shaders call finishPixel() from the shared post-processing
  // chunk.
  world_program_.Build(shaders::kWorldGeometryVert, shaders::kWorldLightingFrag,
                       {{0, "aPos"}, {1, "aNormal"}, {2, "aUV"}},
                       shaders::kToneMapAndGrainFrag);
  shadow_program_.Build(shaders::kWorldGeometryVert,
                        shaders::kShadowDistanceFrag,
                        {{0, "aPos"}, {1, "aNormal"}, {2, "aUV"}});
  composite_program_.Build(shaders::kFullscreenQuadVert,
                           shaders::kLensAndFxaaFrag,
                           {{0, "aPos"}, {2, "aUV"}});
  quad_program_.Build(shaders::kFullscreenQuadVert, shaders::kOverlayFrag,
                      {{0, "aPos"}, {2, "aUV"}});
  sky_program_.Build(shaders::kFullscreenQuadVert, shaders::kNightSkyFrag,
                     {{0, "aPos"}, {2, "aUV"}}, shaders::kToneMapAndGrainFrag);
  mirror_program_.Build(shaders::kMirrorGlassVert, shaders::kMirrorGlassFrag,
                        {{0, "aPos"}});
  bloom_program_.Build(shaders::kFullscreenQuadVert, shaders::kBloomBlurFrag,
                       {{0, "aPos"}, {2, "aUV"}});
  const bool text_ok = text_.Init();
  if (world_program_.Valid()) LookUpWorldUniforms();
  BuildFullscreenQuad();
  UploadTextures(textures);

  // The mirror: its glass is six vertices rewritten each frame; what it shows
  // is drawn into mirrorTarget_.
  mirror_vbo_.Upload(nullptr, 6 * sizeof(Vertex), kDynamicDraw);
  mirror_vao_.Create();
  mirror_vao_.Bind();
  SetVertexLayout(mirror_vbo_.Id());
  BindVertexArray(0);
  mirror_target_.Create(kInternalW / 2, kInternalH / 2);

  // Overlay texture: re-uploaded every frame with nearest filtering.
  overlay_tex_.Create();
  overlay_tex_.SetParams(kNearest, kNearest, kClampToEdge);

  // Shadow maps are made on first use (ensureShadowMaps).

  // Internal scene target (its size is a graphics setting; see setRenderScale).
  target_.Create(kInternalW, kInternalH);
  bloom_a_.Create(kInternalW / 4, kInternalH / 4);
  bloom_b_.Create(kInternalW / 4, kInternalH / 4);
  return world_program_.Valid() && quad_program_.Valid() &&
         composite_program_.Valid() && sky_program_.Valid() && text_ok;
}

// The world program's uniform locations (per-frame ones into world_u_) and its
// fixed sampler units.
void SceneRenderer::LookUpWorldUniforms() {
  u_model_ = world_program_.Uniform("uModel");
  u_emissive_ = world_program_.Uniform("uEmissive");
  u_alpha_test_ = world_program_.Uniform("uAlphaTest");
  WorldUniforms& u = world_u_;
  u.view = world_program_.Uniform("uView");
  u.proj = world_program_.Uniform("uProj");
  u.cam = world_program_.Uniform("uCam");
  u.flash_dir = world_program_.Uniform("uFlashDir");
  u.flash_on = world_program_.Uniform("uFlashOn");
  u.lighter = world_program_.Uniform("uLighter");
  u.torch_warm = world_program_.Uniform("uTorchWarm");
  u.outdoor = world_program_.Uniform("uOutdoor");
  u.moon_dir = world_program_.Uniform("uMoonDir");
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
  u.hand_faces = world_program_.Uniform("uHandFaces");
  u.res = world_program_.Uniform("uRes");
  u.grain = world_program_.Uniform("uGrain");
  u.materials = world_program_.Uniform("uMaterials");
  u.clip = world_program_.Uniform("uClip");
  // The samplers never change: the texture on unit 0, the shadow cubes on units
  // 1 to 5.
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
  // Fullscreen quad: position (x, y) and uv per vertex, two triangles.
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

// Uploads a surface as a mipmapped, repeating texture (internalFormat: sRGB for
// colour, linear for data maps).
GlTexture SceneRenderer::MakeMipmappedTexture(int w, int h, const void* rgba,
                                              gl::GLenum internal_format) {
  GlTexture tex;
  tex.Create();
  tex.Allocate(w, h, rgba, internal_format);
  GenerateMipmap(kTexture2D);
  tex.SetParams(kLinearMipmapLinear, kLinear, kRepeat);
  return tex;
}

void SceneRenderer::UploadTextures(const TextureLibrary& textures) {
  // Every texture the mesh builder can reference. GL texture row 0 sits at v =
  // 0 (the bottom of a wall), and Surface row 0 is the top of the image, so
  // flip while converting.
  std::vector<uint8_t> rgba_bytes;
  for (const auto& name : textures.Names()) {
    if (IsPbrCompanion(name)) {
      // A companion map is read with its texture below; one with no texture is
      // drawn as itself.
      const std::string base = name.substr(0, name.rfind('_'));
      if (textures.Has(base)) continue;
    }
    const Surface& s = textures.Get(name);
    SurfaceToRgbaFlipped(s, rgba_bytes);
    textures_.emplace_back(
        name, MakeMipmappedTexture(s.w, s.h, rgba_bytes.data(), kSrgb8Alpha8));

    // Its normal map (alpha: gloss). Authored maps (<name>_nrm, <name>_rough)
    // win; otherwise the normal is read from the texture's own brightness and
    // there is no specular (gfx/pbr.h).
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
    normal_maps_.emplace_back(
        name, MakeMipmappedTexture(s.w, s.h, rgba_bytes.data()));

    // Its material map (R metal, G occlusion), only when it has a companion for
    // one of them.
    const Surface mat = MakeMaterialMap(
        s, textures.Has(metal_name) ? &textures.Get(metal_name) : nullptr,
        textures.Has(ao_name) ? &textures.Get(ao_name) : nullptr);
    if (!mat.Empty()) {
      SurfaceToRgbaFlipped(mat, rgba_bytes);
      material_maps_.emplace_back(
          name, MakeMipmappedTexture(mat.w, mat.h, rgba_bytes.data()));
    }
  }
  {
    const uint8_t open[4] = {0, 255, 255, 255};  // metal 0, occlusion 1
    default_material_.Create();
    default_material_.Allocate(1, 1, open);
    default_material_.SetParams(kNearest, kNearest, kRepeat);
  }
}

void SceneRenderer::SetRenderScale(int preset) {
  render_scale_ = std::clamp(preset, 0, kRenderScaleCount - 1);
}

void SceneRenderer::ResizeTarget(int window_w, int window_h) {
  if (window_w <= 0 || window_h <= 0) return;
  const int h = kRenderScaleLines[render_scale_];
  const int w = std::max(1, static_cast<int>(std::lround(
                                static_cast<double>(h) * window_w / window_h)));
  target_.Resize(w, h);
  bloom_a_.Resize(std::max(1, w / 4), std::max(1, h / 4));
  bloom_b_.Resize(std::max(1, w / 4), std::max(1, h / 4));
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
    // Bucket whole triangles by the chunk holding their first vertex; the
    // ordered map then lays the chunks out one after another in a single
    // buffer.
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
    dst.push_back(std::move(g));
  }
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
  UploadBatches(world_, geometry.world);
  UploadBatches(props_, geometry.props);
}

void SceneRenderer::UploadCab(const SceneGeometry& geometry) {
  ++scene_revision_;
  cab_.clear();
  UploadBatches(cab_, geometry.world);
  UploadBatches(cab_, geometry.props);
}

void SceneRenderer::UploadDoorPanel(const std::vector<Batch>& batches) {
  ++scene_revision_;
  door_.clear();
  UploadBatches(door_, batches);
}

// Binds a batch's textures, flags and VAO, skipping whatever drawState_ says is
// already set.
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
  b.vao.Bind();
}

void SceneRenderer::DrawBatches(const BatchList& batches, const float* model,
                                const Frustum* cull, float offset_z) {
  UniformMatrix4fv(u_model_, 1, kFalse, model);
  for (const auto& b : batches) {
    if (cull && !BoxInFrustum(*cull, b.bmin, b.bmax, offset_z)) continue;
    bool bound =
        false;  // state is set up when the first chunk turns out to be visible
    // Neighbouring visible chunks are one range of the buffer: they go out as a
    // single draw.
    int run_first = 0, run_count = 0;
    const auto flush = [&] {
      if (run_count > 0) DrawArrays(kTriangles, run_first, run_count);
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
  DrawArrays(kTriangles, 0, 6);
}

// Sky first, outdoors: a fullscreen pass with depth off (so it stays behind
// everything).
void SceneRenderer::DrawSky(const CameraState& cam, const Frame& f) {
  Disable(kDepthTest);
  sky_program_.Use();
  // The camera basis, so the shader can turn a pixel into a view ray.
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
  sky_program_.Set("uRes", static_cast<float>(target_.Width()),
                   static_cast<float>(target_.Height()));
  sky_program_.Set("uGrain", f.grain);
  DrawQuad();
}

// This frame's lamps (lamps_, shadow casters first) as the world shader's
// arrays.
void SceneRenderer::SetLampUniforms() {
  const WorldUniforms& u = world_u_;
  const int n = static_cast<int>(lamps_.size());
  float pos[CameraState::kMaxLights * 3], col[CameraState::kMaxLights * 3],
      rad[CameraState::kMaxLights];
  for (int i = 0; i < n; ++i) {
    const PointLight& l = lamps_[static_cast<size_t>(i)];
    pos[i * 3] = static_cast<float>(l.x);
    pos[i * 3 + 1] = static_cast<float>(l.y);
    pos[i * 3 + 2] = static_cast<float>(l.z);
    col[i * 3] = static_cast<float>(l.r);
    col[i * 3 + 1] = static_cast<float>(l.g);
    col[i * 3 + 2] = static_cast<float>(l.b);
    rad[i] = static_cast<float>(
        std::max(0.001, l.radius));  // the shader divides by it
  }
  Uniform1i(u.light_count, n);
  if (n > 0) {
    Uniform3fv(u.light_pos, n, pos);
    Uniform3fv(u.light_color, n, col);
    Uniform1fv(u.light_radius, n, rad);
  }
}

// The lamps' shadow cubes on units 1..4 and the hand light's on unit 5. The
// active unit is left on 5; drawWorld resets it to 0 after the remaining
// uniforms.
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
  Uniform3f(u.fog_color, f.fog[0], f.fog[1], f.fog[2]);
  Uniform1f(u.fog_density, f.fog_density);
  Uniform1f(u.time, static_cast<float>(cam.time));
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
  Uniform1i(u.hand_faces, static_cast<int>(hand_faces_));
  BindShadowCubes();
  Uniform2f(u.res, static_cast<float>(target_.Width()),
            static_cast<float>(target_.Height()));
  Uniform1f(u.grain, f.grain);
  Uniform1i(u.materials, f.materials);
  Uniform4f(u.clip, f.clip[0], f.clip[1], f.clip[2], f.clip[3]);
  ActiveTexture(kTexture0);
  draw_state_ = DrawState{};

  DrawScene(cam, f.planes, f.views);
}

// Everything that stands in the world this frame, culled against the planes:
// what the main pass draws and what casts shadows.
void SceneRenderer::DrawScene(const CameraState& cam, const Frustum& cull,
                              unsigned views) {
  float model[16];
  Mat4Identity(model);
  DrawBatches(cab_, model, &cull);
  model[14] = static_cast<float>(cam.scroll_z);
  DrawBatches(world_, model, &cull, static_cast<float>(cam.scroll_z));
  DrawBatches(props_, model, &cull, static_cast<float>(cam.scroll_z));
  DrawDoors(cam, cull, model);
  if (views == 0) return;
  DrawInstances(cam, views);
}

// The swinging door panels inside the frustum. `model` is scratch space for
// each panel's matrix.
void SceneRenderer::DrawDoors(const CameraState& cam, const Frustum& cull,
                              float* model) {
  for (const auto& door : cam.doors) {
    // The panel swings within kDoorPanelWidth of its hinge.
    const float hinge_x = static_cast<float>(door.hinge_x),
                hinge_z = static_cast<float>(door.hinge_z);
    const float reach = kDoorPanelWidth + kDoorCullMargin;
    const float hinge_y = static_cast<float>(door.y);
    const float bmin[3] = {hinge_x - reach, hinge_y, hinge_z - reach};
    const float bmax[3] = {hinge_x + reach,
                           hinge_y + kDoorPanelHeight + kDoorCullMargin,
                           hinge_z + reach};
    if (!BoxInFrustum(cull, bmin, bmax)) continue;
    DoorModel(door.hinge_x, door.y, door.hinge_z, door.angle, model);
    DrawBatches(door_, model);
  }
}

// The caller's moving models (the hand light, the player's body) that show in
// these views.
void SceneRenderer::DrawInstances(const CameraState& cam, unsigned views) {
  for (const ModelInstance& inst : cam.instances) {
    if ((inst.views & views) == 0 || inst.model < 0 ||
        inst.model >= static_cast<int>(models_.size())) {
      continue;
    }
    DrawBatches(models_[static_cast<size_t>(inst.model)], inst.matrix);
  }
}

// Whether the glass can be seen from here: the eye is on its front, and it is
// inside the view.
bool SceneRenderer::MirrorVisible(const CameraState& cam,
                                  const Frame& f) const {
  const MirrorState& m = cam.mirror;
  if (!m.on || !mirror_program_.Valid() || !mirror_target_.Width()) {
    return false;
  }
  const double front = (cam.x - m.x) * m.nx + (cam.z - m.z) * m.nz;
  if (front < kMirrorMinFront) return false;
  const double radius = std::hypot(
      m.half_w, m.half_h);  // a box around the glass, whatever its facing
  const float bmin[3] = {static_cast<float>(m.x - radius),
                         static_cast<float>(m.y - radius),
                         static_cast<float>(m.z - radius)};
  const float bmax[3] = {static_cast<float>(m.x + radius),
                         static_cast<float>(m.y + radius),
                         static_cast<float>(m.z + radius)};
  return BoxInFrustum(f.planes, bmin, bmax);
}

// The scene as the glass shows it: the view turned about the mirror's plane (so
// the picture is the room reflected, drawn with the real lights), without what
// lies behind the glass, into mirrorTarget_.
void SceneRenderer::DrawMirrorView(const CameraState& cam, const Frame& f) {
  const MirrorState& m = cam.mirror;
  const int h = std::max(kMirrorMinSide,
                         target_.Height() * kMirrorScaleNum / kMirrorScaleDen);
  const int w = std::max(kMirrorMinSide,
                         target_.Width() * kMirrorScaleNum / kMirrorScaleDen);
  mirror_target_.Resize(w, h);
  const float n[3] = {static_cast<float>(m.nx), 0.0f, static_cast<float>(m.nz)};
  const float d = n[0] * static_cast<float>(m.x) +
                  n[2] * static_cast<float>(m.z);  // the plane: n.p = d
  float reflect[16];                               // p' = p - 2 (n.p - d) n
  Mat4Identity(reflect);
  reflect[0] = 1 - 2 * n[0] * n[0];
  reflect[2] = -2 * n[0] * n[2];
  reflect[8] = -2 * n[2] * n[0];
  reflect[10] = 1 - 2 * n[2] * n[2];
  reflect[12] = 2 * d * n[0];
  reflect[14] = 2 * d * n[2];
  Frame fm = f;
  Mat4Multiply(f.view, reflect, fm.view);
  float view_proj[16];
  Mat4Multiply(f.proj, fm.view, view_proj);
  FrustumPlanes(view_proj, fm.planes);
  // Keep what is in front of the glass: the frame and the picture of the mirror
  // prop itself (which lie on the plane) and the wall it hangs on go, or they
  // would hide the whole reflection.
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

// The glass: a quad a finger in front of the mirror prop's own face, showing
// what drawMirrorView made.
void SceneRenderer::DrawMirrorGlass(const CameraState& cam, const Frame& f) {
  const MirrorState& m = cam.mirror;
  const auto nx = static_cast<float>(m.nx), nz = static_cast<float>(m.nz);
  const float along_x = nz, along_z = -nx;  // the glass's horizontal axis
  const float center_x = static_cast<float>(m.x) + nx * kMirrorGlassOffset;
  const float center_z = static_cast<float>(m.z) + nz * kMirrorGlassOffset;
  const auto half_w = static_cast<float>(m.half_w),
             half_h = static_cast<float>(m.half_h);
  const float center_y = static_cast<float>(m.y);
  const auto corner = [&](float sx,
                          float sy) {  // sx, sy: -1 or 1, the corner's side
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
  ActiveTexture(kTexture0);
  mirror_target_.Color().Bind();
  Enable(kDepthTest);
  DepthFunc(kLequal);
  Enable(kBlend);
  BlendFunc(kSrcAlpha, kOneMinusSrcAlpha);
  mirror_vao_.Bind();
  DrawArrays(kTriangles, 0, 6);
  Disable(kBlend);
}

// Motion blur: the turn since the last frame, as a distance across the picture.
// A turn of the view moves everything the same way, so a straight smear along
// it suffices (the sign does not matter: the taps straddle each pixel). Long
// smears are capped. Remembers this frame's view for the next call.
void SceneRenderer::MotionBlurSmear(const CameraState& cam, const Frame& f,
                                    float& smear_x, float& smear_y) {
  smear_x = smear_y = 0.0f;
  if (cam.motion_blur > 0.0 && have_last_view_) {
    double dyaw = cam.yaw - last_yaw_;
    dyaw = std::remainder(dyaw, kTwoPi);
    const double dpitch = cam.pitch - last_pitch_;
    const double fov_x = 2.0 * std::atan(std::tan(f.fov_y * 0.5) * f.aspect);
    // The strength scales the smear; the cap grows with it so the strongest
    // still reads.
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

// Bloom: the bright parts of the (tone mapped) picture, blurred twice along
// each axis at a quarter of its size, into bloom_b_.
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
  // Pass 1: bright pass + horizontal blur, picture -> a. Then vertical, a -> b,
  // and a second round for a wider, smoother glow.
  struct Pass {
    const RenderTarget* from;
    RenderTarget* to;
    float dx, dy, threshold;
  };
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

// Composite the scene to the window (linear upscale keeps it soft, PS2-like),
// with the blur and lens.
void SceneRenderer::Composite(const CameraState& cam, const Frame& f,
                              int window_w, int window_h) {
  RenderTarget::BindWindow();
  Viewport(0, 0, window_w, window_h);
  ClearColor(0, 0, 0, 1);
  Clear(kColorBufferBit | kDepthBufferBit);
  Disable(kDepthTest);
  Disable(kBlend);

  float bx = 0.0f, by = 0.0f;
  MotionBlurSmear(cam, f, bx, by);

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
  composite_program_.Set("uTape", cam.tape ? 1.0f : 0.0f);
  composite_program_.Set("uTapeTime",
                         static_cast<float>(std::max(0.0, cam.tape_time)));
  composite_program_.Set("uAspect", static_cast<float>(window_w) /
                                        static_cast<float>(std::max(1, window_h)));
  // FXAA only on a still picture: a blurred one has no edges to smooth.
  composite_program_.Set(
      "uFxaa", cam.anti_alias == 1 && bx == 0.0f && by == 0.0f ? 1.0f : 0.0f);
  composite_program_.Set("uTexel", 1.0f / static_cast<float>(target_.Width()),
                         1.0f / static_cast<float>(target_.Height()));
  DrawQuad();
}

// Uploads the overlay as it is (0xAARRGGBB read as BGRA 8-8-8-8 reversed; the
// shader flips it because row 0 is the top). A frame identical to the last one
// is not uploaded again.
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

// The 2D overlay with alpha blending, then its text at full window resolution.
void SceneRenderer::DrawOverlay(const Surface& overlay, const TextLayer* text) {
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

// Everything the frame's passes share: the eye, the hand light's aim, fog, and
// the view and projection matrices with the frustum they imply. Needs target_
// at this frame's size.
SceneRenderer::Frame SceneRenderer::MakeFrame(const CameraState& cam) const {
  Frame f;
  f.eye[0] = static_cast<float>(cam.x);
  f.eye[1] = static_cast<float>(cam.eye_height);
  f.eye[2] = static_cast<float>(cam.z);
  f.dir[0] = static_cast<float>(std::cos(cam.yaw) * std::cos(cam.pitch));
  f.dir[1] = static_cast<float>(std::sin(cam.pitch));
  f.dir[2] = static_cast<float>(std::sin(cam.yaw) * std::cos(cam.pitch));
  {  // the hand light's own aim: the view's, turned by the hand's lag
    const double flash_yaw = cam.yaw + cam.hand_yaw;
    const double flash_pitch =
        std::clamp(cam.pitch + cam.hand_pitch, -kMaxHandPitch, kMaxHandPitch);
    f.flash[0] =
        static_cast<float>(std::cos(flash_yaw) * std::cos(flash_pitch));
    f.flash[1] = static_cast<float>(std::sin(flash_pitch));
    f.flash[2] =
        static_cast<float>(std::sin(flash_yaw) * std::cos(flash_pitch));
  }
  std::ranges::copy(kMoonDir, f.moon);
  // Fog: a cold blue night outdoors, a near-black warm murk indoors
  // (linear-space colours).
  f.fog[0] = cam.outdoor ? 0.012f : 0.006f;
  f.fog[1] = cam.outdoor ? 0.018f : 0.005f;
  f.fog[2] = cam.outdoor ? 0.034f : 0.005f;
  f.fog_density = cam.outdoor ? kOutdoorFogDensity : kRoomFogDensity;
  f.fov_y = kFovY;
  f.views = ModelInstance::kMain;
  f.aspect = static_cast<float>(target_.Width()) /
             static_cast<float>(target_.Height());
  f.grain = cam.grain ? 1.0f : 0.0f;
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

// The player's view into the internal target: sky outdoors, then the world,
// then the mirror glass.
void SceneRenderer::DrawMainView(const CameraState& cam, const Frame& f,
                                 bool mirrored) {
  target_.Bind();
  Viewport(0, 0, target_.Width(), target_.Height());
  ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  Clear(kColorBufferBit | kDepthBufferBit);
  Disable(kBlend);
  if (cam.outdoor) DrawSky(cam, f);
  DrawWorld(cam, f);
  if (mirrored) DrawMirrorGlass(cam, f);
  target_.Resolve();  // a multisampled picture becomes the texture the
                      // composite reads
}

void SceneRenderer::Render(const CameraState& cam, int window_w, int window_h,
                           const Surface* overlay, const TextLayer* text) {
  target_.SetSamples(
      kAntiAliasSamples[std::clamp(cam.anti_alias, 0, kAntiAliasCount - 1)]);
  ResizeTarget(window_w, window_h);
  const Frame f = MakeFrame(cam);

  // 1) The lamps' shadow maps, 2) the world into the internal target.
  ChooseLamps(cam, f);
  DrawShadows(cam);
  const bool mirrored = MirrorVisible(cam, f);
  if (mirrored) {
    DrawMirrorView(cam, f);  // before the main pass: it has its own target
  }
  DrawMainView(cam, f, mirrored);

  // 3) The scene to the window, 4) the 2D overlay on top.
  Composite(cam, f, window_w, window_h);
  if (overlay && !overlay->Empty()) DrawOverlay(*overlay, text);
}

DoorInstance MakeDoorInstance(const DoorState& d) {
  const double eased =
      d.open * d.open * (3.0 - 2.0 * d.open);  // smoothstep: ease the swing
  const double swing = kHalfPi * eased;
  const double along =
      kHingeBase + kHingeSwingShift * d.swing;  // the post face it swings past
  struct DoorInstance di;
  if (d.passage_along_x) {  // shut, the panel runs south from the north post
    di.hinge_x = d.x + along;
    di.hinge_z = d.y + kHingeAcross;
    di.angle = kHalfPi - d.swing * swing;
  } else {  // shut, the panel runs east from the west post
    di.hinge_x = d.x + kHingeAcross;
    di.hinge_z = d.y + along;
    di.angle = d.swing * swing;
  }
  return di;
}

}  // namespace mythos
