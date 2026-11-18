#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/core/mat4.h"
#include "engine/platform/gl/scene_renderer.h"
#include "engine/ui/settings.h"

// The shadow half of the scene renderer: which lamps cast shadows, their cube maps, and drawing them.
namespace mythos {
namespace {

using namespace gl;

// What a shadow level costs: how many lamps cast shadows (the hand light always does) and the
// texels per side of each cube face. Ultra max is 4 lamps and the hand light at 1536^2 x 6 faces
// of 32-bit floats: about 280 MB of video memory.
struct ShadowSpec {
    int lamps, size;
};
constexpr ShadowSpec kShadowSpecs[kShadowLevelCount] = {{0, 0}, {1, 256}, {2, 256}, {2, 512}, {3, 1024}, {4, 1536}};

constexpr unsigned kAllFaces = 0x3F;  // bit i = cube face i
constexpr double kLampOutSum = 0.01;  // r+g+b below this: the lamp is out and casts nothing
constexpr double kNotACaster = 1e30;  // sort key of a lamp that is out (sorts last)
constexpr double kCasterLimit = 1e29;  // keys below this are real distances

// The hand light's place relative to the eye (metres): low and to the right.
constexpr float kHandSideRest = 0.14f, kHandForward = 0.05f, kHandBelow = 0.20f;
// Reach of the hand light's shadow: as far as its light is worth anything.
constexpr float kLighterReach = 7.0f, kTorchReach = 14.0f;
// Cosine at which each hand light's beam fades to nothing (the shader's smoothstep start).
constexpr float kLighterConeCos = 0.30f, kTorchConeCos = 0.91f;
constexpr float kDegToRad = 3.14159265f / 180.0f;
constexpr float kFaceHalfAngleDeg = 54.74f;  // a cube face's square view fits in this cone about its axis
constexpr float kFaceMarginDeg = 8.0f;       // covers the soft edge and the filter taps
constexpr float kMinShadowReach = 0.5f;      // metres; a cube's far plane never closer than this
constexpr float kCubeFaceFov = 1.5707963f;  // 90 degrees: six faces tile the sphere
constexpr float kShadowNear = 0.05f;
constexpr float kShadowFar = 1e4f;           // distance written where a face sees nothing

constexpr uint64_t kFnvOffset = 1469598103934665603ull, kFnvPrime = 1099511628211ull;

// FNV-1a over the bytes of a value.
void mix(uint64_t& h, const void* data, size_t n) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * kFnvPrime;
}
template <class T>
void mix(uint64_t& h, const T& v) {
    mix(h, &v, sizeof(T));
}

}  // namespace

// This frame's lamps, at most kMaxLights. The kMaxShadowLights nearest lit ones come first and
// cast shadows (when the setting is on and their cube maps exist); the shader relies on that order.
void SceneRenderer::chooseLamps(const CameraState& cam, const Frame& f) {
    lamps_.clear();
    lamps_.insert(lamps_.end(), cam.lights.begin(),
                  cam.lights.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(cam.lights.size(), CameraState::kMaxLights)));
    shadowCount_ = 0;
    ensureShadowMaps(shadowProgram_.valid() ? cam.shadowLevel : 0);
    // The hand light is held low and to the right (inside the player's radius, so never in a
    // wall), so what it lights throws a shadow you can see rather than one hidden behind it.
    handShadowOn_ = cam.flashOn && handShadow_.valid();
    handFaces_ = kAllFaces;
    if (handShadowOn_) placeHandShadow(cam, f);
    if (shadowLimit_ == 0 || !shadow_[0].valid()) return;
    // Squared distance from the eye; a lamp that is out sorts last.
    auto distanceKey = [&](const PointLight& l) {
        if (l.r + l.g + l.b < kLampOutSum) return kNotACaster;
        const double dx = l.x - f.eye[0], dy = l.y - f.eye[1], dz = l.z - f.eye[2];
        return dx * dx + dy * dy + dz * dz;
    };
    std::stable_sort(lamps_.begin(), lamps_.end(),
                     [&](const PointLight& a, const PointLight& b) { return distanceKey(a) < distanceKey(b); });
    for (const PointLight& l : lamps_)
        if (shadowCount_ < shadowLimit_ && distanceKey(l) < kCasterLimit) ++shadowCount_;
}

// Where the hand light is held, how far its shadow reaches, and which cube faces its beam can reach.
void SceneRenderer::placeHandShadow(const CameraState& cam, const Frame& f) {
    const float rightX = -std::sin(static_cast<float>(cam.yaw)), rightZ = std::cos(static_cast<float>(cam.yaw));
    const float side = kHandSideRest + static_cast<float>(cam.handSide);
    handPos_[0] = f.eye[0] + rightX * side + f.dir[0] * kHandForward;
    handPos_[1] = f.eye[1] - kHandBelow - static_cast<float>(cam.handDown);
    handPos_[2] = f.eye[2] + rightZ * side + f.dir[2] * kHandForward;
    handReach_ = cam.lighter ? kLighterReach : kTorchReach;
    // The beam lights nothing outside its cone (the shader's smoothstep starts at this cosine),
    // so only the cube faces the cone can reach are drawn.
    const float coneCos = cam.lighter ? kLighterConeCos : kTorchConeCos;
    const float reachAngle = std::acos(coneCos) + (kFaceHalfAngleDeg + kFaceMarginDeg) * kDegToRad;
    if (reachAngle < 3.1415f) {
        const float limit = std::cos(reachAngle);
        static const float axes[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        handFaces_ = 0;
        for (int i = 0; i < 6; ++i)
            if (f.flash[0] * axes[i][0] + f.flash[1] * axes[i][1] + f.flash[2] * axes[i][2] > limit) handFaces_ |= 1u << i;
    }
}

void SceneRenderer::ensureShadowMaps(int level) {
    level = std::clamp(level, 0, kShadowLevelCount - 1);
    if (level == shadowBuilt_) return;
    shadowBuilt_ = level;
    for (ShadowCube& c : shadow_) c.reset();
    handShadow_.reset();
    shadowDepth_.reset();
    std::memset(shadowKey_, 0, sizeof(shadowKey_));
    std::memset(handKey_, 0, sizeof(handKey_));
    const ShadowSpec spec = kShadowSpecs[level];
    shadowLimit_ = std::min(spec.lamps, CameraState::kMaxShadowLights);
    shadowSize_ = std::max(spec.size, 1);
    if (level == 0) return;
    shadowDepth_.create(spec.size, spec.size);
    handShadow_.create(spec.size, shadowDepth_);
    for (int i = 0; i < shadowLimit_; ++i) shadow_[i].create(spec.size, shadowDepth_);
    // A map that could not be made (out of memory, say) leaves the lamps unshadowed.
    for (int i = 0; i < shadowLimit_; ++i)
        if (!shadow_[i].valid()) shadowLimit_ = 0;
}

uint64_t SceneRenderer::sceneKey(const CameraState& cam) const {
    uint64_t h = kFnvOffset;
    mix(h, sceneRevision_);
    mix(h, cam.scrollZ);
    for (const DoorInstance& d : cam.doors) {
        mix(h, d.hingeX);
        mix(h, d.hingeZ);
        mix(h, d.angle);
    }
    return h;
}

// Six passes per shadow lamp: the distance from the lamp to what it sees along each cube face.
// A cube whose lamp and scene are as they were when it was drawn is left alone.
void SceneRenderer::drawShadows(const CameraState& cam) {
    if (shadowCount_ == 0 && !handShadowOn_) return;
    const uint64_t scene = sceneKey(cam);
    auto keyFor = [&](const float* pos, float reach) {
        uint64_t h = scene;
        mix(h, pos, 3 * sizeof(float));
        mix(h, reach);
        return h | 1;  // never 0, which means "not drawn"
    };
    // The batches' uniforms are the shadow program's for the duration.
    const GLint savedModel = uModel_, savedEmissive = uEmissive_, savedAlpha = uAlphaTest_;
    uModel_ = shadowProgram_.uniform("uModel");
    uEmissive_ = -1;
    uAlphaTest_ = shadowProgram_.uniform("uAlphaTest");
    bindNormals_ = false;
    shadowProgram_.use();
    shadowProgram_.set("uTex", 0);
    Enable(DEPTH_TEST);
    DepthFunc(LEQUAL);
    Disable(BLEND);
    ActiveTexture(TEXTURE0);
    for (int i = 0; i < shadowCount_; ++i) {
        const PointLight& l = lamps_[static_cast<size_t>(i)];
        const float pos[3] = {static_cast<float>(l.x), static_cast<float>(l.y), static_cast<float>(l.z)};
        drawShadowCube(cam, shadow_[i], pos, static_cast<float>(l.radius), kAllFaces, shadowKey_[i],
                       keyFor(pos, static_cast<float>(l.radius)));
    }
    if (handShadowOn_)
        drawShadowCube(cam, handShadow_, handPos_, handReach_, handFaces_, handKey_, keyFor(handPos_, handReach_));
    uModel_ = savedModel;
    uEmissive_ = savedEmissive;
    uAlphaTest_ = savedAlpha;
    bindNormals_ = true;
}

void SceneRenderer::drawShadowCube(const CameraState& cam, ShadowCube& cube, const float* eye, float reachIn,
                                   unsigned faceMask, uint64_t* keys, uint64_t baseKey) {
    const float reach = std::max(kMinShadowReach, reachIn);
    bool setUp = false;
    float proj[16];
    for (int face = 0; face < 6; ++face) {
        if (!(faceMask & (1u << face)) || keys[face] == baseKey) continue;
        keys[face] = baseKey;
        if (!setUp) {
            setUp = true;
            shadowProgram_.set("uLightPos", eye[0], eye[1], eye[2]);
            mat4Perspective(kCubeFaceFov, 1.0f, kShadowNear, reach, proj);
        }
        float view[16], viewProj[16];
        Frustum frustum;
        cubeFaceView(face, eye, view);
        mat4Multiply(proj, view, viewProj);
        frustumPlanes(viewProj, frustum);
        shadowProgram_.setMat4("uView", view);
        shadowProgram_.setMat4("uProj", proj);
        cube.bindFace(face);
        ClearColor(kShadowFar, 0.0f, 0.0f, 1.0f);
        Clear(COLOR_BUFFER_BIT | DEPTH_BUFFER_BIT);
        drawState_ = DrawState{};
        drawScene(cam, frustum, 0);
    }
}

}  // namespace mythos
