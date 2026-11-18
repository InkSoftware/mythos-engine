#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

#include "engine/core/mat4.h"
#include "engine/platform/gl/scene_renderer.h"
#include "engine/ui/settings.h"

// The shadow half of the scene renderer: which lamps cast shadows, their cube
// maps, and drawing them.
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

// What a shadow level costs: how many lamps cast shadows (the hand light always
// does) and the texels per side of each cube face. Ultra max is 4 lamps and the
// hand light at 1536^2 x 6 faces of 32-bit floats: about 280 MB of video
// memory.
struct ShadowSpec {
  int lamps, size;
};
constexpr ShadowSpec kShadowSpecs[kShadowLevelCount] = {
    {.lamps = 0, .size = 0},    {.lamps = 1, .size = 256},
    {.lamps = 2, .size = 256},  {.lamps = 2, .size = 512},
    {.lamps = 3, .size = 1024}, {.lamps = 4, .size = 1536}};

constexpr unsigned kAllFaces = 0x3F;  // bit i = cube face i
constexpr double kLampOutSum =
    0.01;  // r+g+b below this: the lamp is out and casts nothing
constexpr double kNotACaster =
    1e30;  // sort key of a lamp that is out (sorts last)
constexpr double kCasterLimit = 1e29;  // keys below this are real distances

// The hand light's place relative to the eye (metres): low and to the right.
constexpr float kHandSideRest = 0.14f, kHandForward = 0.05f, kHandBelow = 0.20f;
// Reach of the hand light's shadow: as far as its light is worth anything.
constexpr float kLighterReach = 7.0f, kTorchReach = 14.0f;
// Cosine at which each hand light's beam fades to nothing (the shader's
// smoothstep start).
constexpr float kLighterConeCos = 0.30f, kTorchConeCos = 0.91f;
constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;
constexpr float kFaceHalfAngleDeg =
    54.74f;  // a cube face's square view fits in this cone about its axis
constexpr float kFaceMarginDeg =
    8.0f;  // covers the soft edge and the filter taps
constexpr float kMinShadowReach =
    0.5f;  // metres; a cube's far plane never closer than this
constexpr float kCubeFaceFov =
    1.5707963f;  // 90 degrees: six faces tile the sphere
constexpr float kShadowNear = 0.05f;
constexpr float kShadowFar =
    1e4f;  // distance written where a face sees nothing

constexpr uint64_t kFnvOffset = 1469598103934665603ull,
                   kFnvPrime = 1099511628211ull;

// FNV-1a over the bytes of a value.
void Mix(uint64_t& h, const void* data, size_t n) {
  const auto* p = static_cast<const unsigned char*>(data);
  for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * kFnvPrime;
}
template <class T>
void Mix(uint64_t& h, const T& v) {
  Mix(h, &v, sizeof(T));
}

}  // namespace

// This frame's lamps, at most kMaxLights. The kMaxShadowLights nearest lit ones
// come first and cast shadows (when the setting is on and their cube maps
// exist); the shader relies on that order.
void SceneRenderer::ChooseLamps(const CameraState& cam, const Frame& f) {
  lamps_.clear();
  lamps_.insert(
      lamps_.end(), cam.lights.begin(),
      cam.lights.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(
                               cam.lights.size(), CameraState::kMaxLights)));
  shadow_count_ = 0;
  EnsureShadowMaps(shadow_program_.Valid() ? cam.shadow_level : 0);
  // The hand light is held low and to the right (inside the player's radius, so
  // never in a wall), so what it lights throws a shadow you can see rather than
  // one hidden behind it.
  hand_shadow_on_ = cam.flash_on && hand_shadow_.Valid();
  hand_faces_ = kAllFaces;
  if (hand_shadow_on_) PlaceHandShadow(cam, f);
  if (shadow_limit_ == 0 || !shadow_[0].Valid()) return;
  // Squared distance from the eye; a lamp that is out sorts last.
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

// Where the hand light is held, how far its shadow reaches, and which cube
// faces its beam can reach.
void SceneRenderer::PlaceHandShadow(const CameraState& cam, const Frame& f) {
  const float right_x = -std::sin(static_cast<float>(cam.yaw)),
              right_z = std::cos(static_cast<float>(cam.yaw));
  const float side = kHandSideRest + static_cast<float>(cam.hand_side);
  hand_pos_[0] = f.eye[0] + right_x * side + f.dir[0] * kHandForward;
  hand_pos_[1] = f.eye[1] - kHandBelow - static_cast<float>(cam.hand_down);
  hand_pos_[2] = f.eye[2] + right_z * side + f.dir[2] * kHandForward;
  hand_reach_ = cam.lighter ? kLighterReach : kTorchReach;
  // The beam lights nothing outside its cone (the shader's smoothstep starts at
  // this cosine), so only the cube faces the cone can reach are drawn.
  const float cone_cos = cam.lighter ? kLighterConeCos : kTorchConeCos;
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
  // A map that could not be made (out of memory, say) leaves the lamps
  // unshadowed.
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

// Six passes per shadow lamp: the distance from the lamp to what it sees along
// each cube face. A cube whose lamp and scene are as they were when it was
// drawn is left alone.
void SceneRenderer::DrawShadows(const CameraState& cam) {
  if (shadow_count_ == 0 && !hand_shadow_on_) return;
  const uint64_t scene = SceneKey(cam);
  const auto key_for = [&](const float* pos, float reach) {
    uint64_t h = scene;
    Mix(h, pos, 3 * sizeof(float));
    Mix(h, reach);
    return h | 1;  // never 0, which means "not drawn"
  };
  // The batches' uniforms are the shadow program's for the duration.
  const GLint saved_model = u_model_, saved_emissive = u_emissive_,
              saved_alpha = u_alpha_test_;
  u_model_ = shadow_program_.Uniform("uModel");
  u_emissive_ = -1;
  u_alpha_test_ = shadow_program_.Uniform("uAlphaTest");
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
  bind_normals_ = true;
}

void SceneRenderer::DrawShadowCube(const CameraState& cam, ShadowCube& cube,
                                   const float* eye, float reach_in,
                                   unsigned face_mask, uint64_t* keys,
                                   uint64_t base_key) {
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
    DrawScene(cam, frustum, 0);
  }
}

}  // namespace mythos
