#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

#include "engine/core/mat4.h"
#include "engine/platform/gl/scene_renderer.h"
#include "engine/ui/settings.h"

namespace mythos {
namespace {
using gl::ActiveTexture;
using gl::Clear;
using gl::ClearColor;
using gl::DepthFunc;
using gl::Disable;
using gl::Enable;
using gl::GLint;
using gl::kBlend;
using gl::kColorBufferBit;
using gl::kDepthBufferBit;
using gl::kDepthTest;
using gl::kLequal;
using gl::kTexture0;

// Lamps with shadows and the cube face size, per shadow level (settings.h).
struct ShadowSpec {
  int lamps, size;
};

constexpr ShadowSpec kShadowSpecs[kShadowLevelCount] = {
    {.lamps = 0, .size = 0},    {.lamps = 1, .size = 256},
    {.lamps = 2, .size = 256},  {.lamps = 2, .size = 512},
    {.lamps = 3, .size = 1024}, {.lamps = 4, .size = 1536}};

// Bit mask with all six cube faces.
constexpr unsigned kAllFaces = 0x3F;
// Lamps whose color components sum below this are off and never cast.
constexpr double kLampOutSum = 0.01;
// Sort key for lamps that cannot cast; anything above the limit is skipped.
constexpr double kNotACaster = 1e30;
constexpr double kCasterLimit = 1e29;

// Hand light origin relative to the eye: right, forward and down, in world
// units.
constexpr float kHandSideRest = 0.14f, kHandForward = 0.05f, kHandBelow = 0.20f;

// Reach of the hand light, and the cosines of its cone half angle. These must
// stay in step with the world shader's hand light cone.
constexpr float kLighterReach = 7.0f, kTorchReach = 14.0f;

constexpr float kLighterConeCos = 0.30f, kTorchConeCos = 0.91f;
constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;
// Half the angle of a cube face's corner as seen from the center (atan of sqrt
// 2), plus a margin, used to decide which faces the cone can reach.
constexpr float kFaceHalfAngleDeg = 54.74f;
constexpr float kFaceMarginDeg = 8.0f;
// Smallest cube far plane, so a tiny radius cannot give a degenerate
// projection.
constexpr float kMinShadowReach = 0.5f;
constexpr float kCubeFaceFov = 1.5707963f;
constexpr float kShadowNear = 0.05f;
// Value the distance maps are cleared to: "very far", so a texel without a
// caster is never in shadow.
constexpr float kShadowFar = 1e4f;

// FNV-1a, to build cache keys.
constexpr uint64_t kFnvOffset = 1469598103934665603ull,
                   kFnvPrime = 1099511628211ull;

void Mix(uint64_t& h, const void* const data, const size_t n) {
  const auto* p = static_cast<const unsigned char*>(data);
  for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * kFnvPrime;
}

template <class T>
void Mix(uint64_t& h, const T& v) {
  Mix(h, &v, sizeof(T));
}
}

void SceneRenderer::ChooseLamps(const CameraState& cam, const Frame& f) {
  lamps_.clear();
  lamps_.insert(
      lamps_.end(), cam.lights.begin(),
      cam.lights.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(
                               cam.lights.size(), CameraState::kMaxLights)));
  shadow_count_ = 0;
  EnsureShadowMaps(shadow_program_.Valid() ? cam.shadow_level : 0);

  hand_shadow_on_ = cam.flash_on && hand_shadow_.Valid();
  hand_faces_ = kAllFaces;
  if (hand_shadow_on_) PlaceHandShadow(cam, f);
  // A game that says where the light is (the torch's tip) overrides the hand
  // position, for the beam and for its shadows.
  hand_origin_on_ = cam.flash_on && cam.hand_origin_set;
  if (hand_origin_on_) {
    hand_pos_[0] = static_cast<float>(cam.hand_x);
    hand_pos_[1] = static_cast<float>(cam.hand_y);
    hand_pos_[2] = static_cast<float>(cam.hand_z);
  }
  // Order lit lamps by squared distance to the eye so the nearest get the
  // shadow cubes (stable, so the choice does not flicker among equals).
  if (shadow_limit_ == 0 || !shadow_[0].Valid()) return;

  auto distance_key = [&](const PointLight& l) {
    if (l.r + l.g + l.b < kLampOutSum) return kNotACaster;
    const double dx = l.x - f.eye[0], dy = l.y - f.eye[1], dz = l.z - f.eye[2];
    return dx * dx + dy * dy + dz * dz;
  };
  std::ranges::stable_sort(lamps_,
                           [&](const PointLight& a, const PointLight& b) {
                             return distance_key(a) < distance_key(b);
                           });
  for (const PointLight& l : lamps_) {
    if (shadow_count_ < shadow_limit_ && distance_key(l) < kCasterLimit) {
      ++shadow_count_;
    }
  }
}

void SceneRenderer::PlaceHandShadow(const CameraState& cam, const Frame& f) {
  const float right_x = -std::sin(static_cast<float>(cam.yaw)),
              right_z = std::cos(static_cast<float>(cam.yaw));
  const float side = kHandSideRest + static_cast<float>(cam.hand_side);
  hand_pos_[0] = f.eye[0] + right_x * side + f.dir[0] * kHandForward;
  hand_pos_[1] = f.eye[1] - kHandBelow - static_cast<float>(cam.hand_down);
  hand_pos_[2] = f.eye[2] + right_z * side + f.dir[2] * kHandForward;
  hand_reach_ = cam.lighter ? kLighterReach : kTorchReach;

  const float cone_cos = cam.lighter ? kLighterConeCos : kTorchConeCos;
  // A face is drawn if its axis is within the cone plus the face's own angular
  // size; with a wide cone all faces are.
  const float reach_angle =
      std::acos(cone_cos) + (kFaceHalfAngleDeg + kFaceMarginDeg) * kDegToRad;
  if (reach_angle < std::numbers::pi_v<float>) {
    const float limit = std::cos(reach_angle);
    static const float kAxes[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                      {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
    hand_faces_ = 0;
    for (int i = 0; i < 6; ++i) {
      if (f.flash[0] * kAxes[i][0] + f.flash[1] * kAxes[i][1] +
              f.flash[2] * kAxes[i][2] >
          limit) {
        hand_faces_ |= 1u << i;
      }
    }
  }
}

void SceneRenderer::EnsureShadowMaps(int level) {
  level = std::clamp(level, 0, kShadowLevelCount - 1);
  if (level == shadow_built_) return;
  shadow_built_ = level;
  for (ShadowCube& c : shadow_) c.Reset();
  hand_shadow_.Reset();
  shadow_depth_.Reset();
  std::memset(shadow_key_, 0, sizeof(shadow_key_));
  std::memset(hand_key_, 0, sizeof(hand_key_));
  const ShadowSpec spec = kShadowSpecs[level];
  shadow_limit_ = std::min(spec.lamps, CameraState::kMaxShadowLights);
  shadow_size_ = std::max(spec.size, 1);
  if (level == 0) return;
  shadow_depth_.Create(spec.size, spec.size);
  hand_shadow_.Create(spec.size, shadow_depth_);
  for (int i = 0; i < shadow_limit_; ++i) {
    shadow_[i].Create(spec.size, shadow_depth_);
  }

  for (int i = 0; i < shadow_limit_; ++i) {
    if (!shadow_[i].Valid()) shadow_limit_ = 0;
  }
}

uint64_t SceneRenderer::SceneKey(const CameraState& cam) const {
  uint64_t h = kFnvOffset;
  Mix(h, scene_revision_);
  Mix(h, cam.scroll_z);
  for (const struct DoorInstance& d : cam.doors) {
    Mix(h, d.hinge_x);
    Mix(h, d.hinge_z);
    Mix(h, d.angle);
  }
  return h;
}

// The shadow maps of the light that lights an outdoor scene (the sun or the
// moon): three orthographic depth maps centred on the viewer (a close, sharp
// one, a wide one and a far one), each redrawn only when the scene, its snapped
// box or a shadow caster changes.
void SceneRenderer::DrawSunShadow(const CameraState& cam, const Frame& f) {
  constexpr int kSunMapSize = 2048;
  constexpr float kExtents[kSunCascades] = {8.0f, 26.0f, 80.0f};
  constexpr float kSunDepth = 80.0f;
  sun_on_ = false;
  if (!cam.outdoor || cam.sun_shadow <= 0.0 || cam.shadow_level <= 0 ||
      !shadow_program_.Valid()) {
    return;
  }
  const float eye[3] = {static_cast<float>(cam.x),
                        static_cast<float>(cam.eye_height),
                        static_cast<float>(cam.z)};
  SunShadow cascades[kSunCascades];
  bool stale[kSunCascades] = {};
  bool any_stale = false;
  uint64_t casters = SceneKey(cam);
  for (const ModelInstance& inst : cam.instances) {
    if (!(inst.views & ModelInstance::kShadow) || inst.model < 0) continue;
    Mix(casters, inst.model);
    Mix(casters, inst.matrix, sizeof(inst.matrix));
    if (!inst.skin.empty()) {
      Mix(casters, inst.skin.data(), inst.skin.size() * sizeof(Mat4f));
    }
  }
  for (int c = 0; c < kSunCascades; ++c) {
    if (sun_target_[c].Width() == 0) {
      sun_target_[c].Create(kSunMapSize, kSunMapSize);
    }
    cascades[c] =
        ComputeSunShadow(eye, f.moon, kExtents[c], kSunDepth, kSunMapSize);
    std::memcpy(sun_view_proj_[c], cascades[c].view_proj,
                sizeof(sun_view_proj_[c]));
    uint64_t key = casters;
    Mix(key, cascades[c].view_proj, sizeof(cascades[c].view_proj));
    key |= 1;
    stale[c] = key != sun_key_[c];
    sun_key_[c] = key;
    any_stale = any_stale || stale[c];
  }
  sun_texel_ = 1.0f / static_cast<float>(kSunMapSize);
  sun_on_ = true;
  if (!any_stale) return;

  const GLint saved_model = u_model_, saved_emissive = u_emissive_,
              saved_alpha = u_alpha_test_, saved_skinned = u_skinned_,
              saved_bones = u_bones_, saved_instanced = u_instanced_,
              saved_morph_count = u_morph_count_,
              saved_morph_verts = u_morph_verts_, saved_morph_w = u_morph_w_;
  u_model_ = shadow_program_.Uniform("uModel");
  u_emissive_ = -1;
  u_alpha_test_ = shadow_program_.Uniform("uAlphaTest");
  u_skinned_ = shadow_program_.Uniform("uSkinned");
  u_bones_ = shadow_program_.Uniform("uBones");
  u_morph_count_ = shadow_program_.Uniform("uMorphCount");
  u_morph_verts_ = shadow_program_.Uniform("uMorphVerts");
  u_morph_w_ = shadow_program_.Uniform("uMorphW");
  u_instanced_ = shadow_program_.Uniform("uInstanced");
  bind_normals_ = false;
  shadow_program_.Use();
  shadow_program_.Set("uTex", 0);
  Enable(kDepthTest);
  DepthFunc(kLequal);
  Disable(kBlend);
  ActiveTexture(kTexture0);
  for (int c = 0; c < kSunCascades; ++c) {
    if (!stale[c]) continue;
    shadow_program_.SetMat4("uView", cascades[c].view);
    shadow_program_.SetMat4("uProj", cascades[c].proj);
    sun_target_[c].Bind();
    gl::Viewport(0, 0, sun_target_[c].Width(), sun_target_[c].Height());
    Clear(kDepthBufferBit);
    draw_state_ = DrawState{};
    Frustum frustum;
    FrustumPlanes(cascades[c].view_proj, frustum);
    counting_shadows_ = true;
    DrawScene(cam, frustum, ModelInstance::kShadow);
    counting_shadows_ = false;
  }
  u_model_ = saved_model;
  u_emissive_ = saved_emissive;
  u_alpha_test_ = saved_alpha;
  u_skinned_ = saved_skinned;
  u_bones_ = saved_bones;
  u_morph_count_ = saved_morph_count;
  u_morph_verts_ = saved_morph_verts;
  u_morph_w_ = saved_morph_w;
  u_instanced_ = saved_instanced;
  bind_normals_ = true;
}

void SceneRenderer::DrawShadows(const CameraState& cam) {
  if (shadow_count_ == 0 && !hand_shadow_on_) return;
  const uint64_t scene = SceneKey(cam);
  // A cache key combines the scene state with a lamp's position and reach; the
  // low bit keeps it non-zero so a fresh key never matches an empty slot.
  // The shadow casters near a lamp (a body that moves) are part of its key, so
  // its cube is redrawn while they move and cached while nothing does.
  const auto key_for = [&](const float* const pos, const float reach) {
    uint64_t h = scene;
    Mix(h, pos, 3 * sizeof(float));
    Mix(h, reach);
    const float near_reach = reach + 2.0f;
    for (const ModelInstance& inst : cam.instances) {
      if (!(inst.views & ModelInstance::kShadow) || inst.model < 0) continue;
      const float dx = inst.matrix[12] - pos[0], dy = inst.matrix[13] - pos[1],
                  dz = inst.matrix[14] - pos[2];
      if (dx * dx + dy * dy + dz * dz > near_reach * near_reach) continue;
      Mix(h, inst.model);
      Mix(h, inst.matrix, sizeof(inst.matrix));
      if (!inst.skin.empty()) {
        Mix(h, inst.skin.data(), inst.skin.size() * sizeof(Mat4f));
      }
    }
    return h | 1;
  };

  // Borrow the batch drawing machinery for the shadow program; restored below.
  const GLint saved_model = u_model_, saved_emissive = u_emissive_,
              saved_alpha = u_alpha_test_, saved_skinned = u_skinned_,
              saved_bones = u_bones_, saved_instanced = u_instanced_,
              saved_morph_count = u_morph_count_,
              saved_morph_verts = u_morph_verts_, saved_morph_w = u_morph_w_;
  u_model_ = shadow_program_.Uniform("uModel");
  u_emissive_ = -1;
  u_alpha_test_ = shadow_program_.Uniform("uAlphaTest");
  u_skinned_ = shadow_program_.Uniform("uSkinned");
  u_bones_ = shadow_program_.Uniform("uBones");
  u_morph_count_ = shadow_program_.Uniform("uMorphCount");
  u_morph_verts_ = shadow_program_.Uniform("uMorphVerts");
  u_morph_w_ = shadow_program_.Uniform("uMorphW");
  u_instanced_ = shadow_program_.Uniform("uInstanced");
  bind_normals_ = false;
  shadow_program_.Use();
  shadow_program_.Set("uTex", 0);
  Enable(kDepthTest);
  DepthFunc(kLequal);
  Disable(kBlend);
  ActiveTexture(kTexture0);
  for (int i = 0; i < shadow_count_; ++i) {
    const PointLight& l = lamps_[static_cast<size_t>(i)];
    const float pos[3] = {static_cast<float>(l.x), static_cast<float>(l.y),
                          static_cast<float>(l.z)};
    DrawShadowCube(cam, shadow_[i], pos, static_cast<float>(l.radius),
                   kAllFaces, shadow_key_[i],
                   key_for(pos, static_cast<float>(l.radius)));
  }
  if (hand_shadow_on_) {
    DrawShadowCube(cam, hand_shadow_, hand_pos_, hand_reach_, hand_faces_,
                   hand_key_, key_for(hand_pos_, hand_reach_));
  }
  u_model_ = saved_model;
  u_emissive_ = saved_emissive;
  u_alpha_test_ = saved_alpha;
  u_skinned_ = saved_skinned;
  u_bones_ = saved_bones;
  u_morph_count_ = saved_morph_count;
  u_morph_verts_ = saved_morph_verts;
  u_morph_w_ = saved_morph_w;
  u_instanced_ = saved_instanced;
  bind_normals_ = true;
}

void SceneRenderer::DrawShadowCube(const CameraState& cam, ShadowCube& cube,
                                   const float* const eye, const float reach_in,
                                   const unsigned face_mask,
                                   uint64_t* const keys,
                                   const uint64_t base_key) {
  const float reach = std::max(kMinShadowReach, reach_in);
  bool set_up = false;
  float proj[16];
  for (int face = 0; face < 6; ++face) {
    if (!(face_mask & (1u << face)) || keys[face] == base_key) continue;
    keys[face] = base_key;
    if (!set_up) {
      set_up = true;
      shadow_program_.Set("uLightPos", eye[0], eye[1], eye[2]);
      Mat4Perspective(kCubeFaceFov, 1.0f, kShadowNear, reach, proj);
    }
    float view[16], view_proj[16];
    Frustum frustum;
    CubeFaceView(face, eye, view);
    Mat4Multiply(proj, view, view_proj);
    FrustumPlanes(view_proj, frustum);
    shadow_program_.SetMat4("uView", view);
    shadow_program_.SetMat4("uProj", proj);
    cube.BindFace(face);
    ClearColor(kShadowFar, 0.0f, 0.0f, 1.0f);
    Clear(kColorBufferBit | kDepthBufferBit);
    draw_state_ = DrawState{};
    ++stats_.shadow_faces;
    counting_shadows_ = true;
    DrawScene(cam, frustum, ModelInstance::kShadow);
    counting_shadows_ = false;
  }
}
}
