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

// FNV-1a over the bytes of a value.
void mix(uint64_t& h, const void* data, size_t n) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
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
    handFaces_ = 0x3F;
    if (handShadowOn_) {
        const float rx = -std::sin(static_cast<float>(cam.yaw)), rz = std::cos(static_cast<float>(cam.yaw));
        const float side = 0.14f + static_cast<float>(cam.handSide);
        handPos_[0] = f.eye[0] + rx * side + f.dir[0] * 0.05f;
        handPos_[1] = f.eye[1] - 0.20f - static_cast<float>(cam.handDown);
        handPos_[2] = f.eye[2] + rz * side + f.dir[2] * 0.05f;
        handReach_ = cam.lighter ? 7.0f : 14.0f;  // as far as its light is worth anything
        // The beam lights nothing outside its cone (the shader's smoothstep starts at this cosine),
        // so only the cube faces the cone can reach are drawn. A face's square view is inside a cone
        // of 54.74 degrees about its axis; the margin covers the soft edge and the filter taps.
        const float coneCos = cam.lighter ? 0.30f : 0.91f;
        const float reachAngle = std::acos(coneCos) + (54.74f + 8.0f) * 3.14159265f / 180.0f;
        if (reachAngle < 3.1415f) {
            const float lim = std::cos(reachAngle);
            static const float axes[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            handFaces_ = 0;
            for (int i = 0; i < 6; ++i)
                if (f.flash[0] * axes[i][0] + f.flash[1] * axes[i][1] + f.flash[2] * axes[i][2] > lim) handFaces_ |= 1u << i;
        }
    }
    if (shadowLimit_ == 0 || !shadow_[0].valid()) return;
    auto key = [&](const PointLight& l) {
        if (l.r + l.g + l.b < 0.01) return 1e30;  // a lamp that is out casts nothing
        const double dx = l.x - f.eye[0], dy = l.y - f.eye[1], dz = l.z - f.eye[2];
        return dx * dx + dy * dy + dz * dz;
    };
    std::stable_sort(lamps_.begin(), lamps_.end(),
                     [&](const PointLight& a, const PointLight& b) { return key(a) < key(b); });
    for (const PointLight& l : lamps_)
        if (shadowCount_ < shadowLimit_ && key(l) < 1e29) ++shadowCount_;
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
    uint64_t h = 1469598103934665603ull;
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
        drawShadowCube(cam, shadow_[i], pos, static_cast<float>(l.radius), 0x3F, shadowKey_[i],
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
    const float reach = std::max(0.5f, reachIn);
    bool setUp = false;
    float proj[16];
    for (int face = 0; face < 6; ++face) {
        if (!(faceMask & (1u << face)) || keys[face] == baseKey) continue;
        keys[face] = baseKey;
        if (!setUp) {
            setUp = true;
            shadowProgram_.set("uLightPos", eye[0], eye[1], eye[2]);
            mat4Perspective(1.5707963f, 1.0f, 0.05f, reach, proj);
        }
        float view[16], viewProj[16];
        Frustum frustum;
        cubeFaceView(face, eye, view);
        mat4Multiply(proj, view, viewProj);
        frustumPlanes(viewProj, frustum);
        shadowProgram_.setMat4("uView", view);
        shadowProgram_.setMat4("uProj", proj);
        cube.bindFace(face);
        ClearColor(1e4f, 0.0f, 0.0f, 1.0f);
        Clear(COLOR_BUFFER_BIT | DEPTH_BUFFER_BIT);
        drawState_ = DrawState{};
        drawScene(cam, frustum, 0);
    }
}

}  // namespace mythos
