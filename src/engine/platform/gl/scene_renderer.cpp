#include "engine/platform/gl/scene_renderer.h"

#include "engine/core/mat4.h"
#include "engine/gfx/normalmap.h"
#include "engine/gfx/pbr.h"
#include "engine/platform/gl/scene_shaders.h"
#include "engine/ui/settings.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>

namespace mythos {
namespace {

using namespace gl;

// Door panel hinged at (hingeX, hingeZ), its spine turned to absolute `angle` on the map plane.
void doorModel(double hingeX, double hingeZ, double angle, float* m) {
    const float c = static_cast<float>(std::cos(angle)), s = static_cast<float>(std::sin(angle));
    mat4Identity(m);
    m[0] = c;   m[2] = s;   // local +x (the spine) -> (cos a, 0, sin a)
    m[8] = -s;  m[10] = c;  // local +z (the thickness) stays perpendicular
    m[12] = static_cast<float>(hingeX);
    m[14] = static_cast<float>(hingeZ);
}

constexpr float kFovY = 62.0f * 3.14159265f / 180.0f;
constexpr float kNearPlane = 0.05f, kFarPlane = 80.0f;  // metres; far is past the longest sightline in a house
constexpr double kTwoPi = 6.283185307179586;
constexpr double kHalfPi = 1.5707963;
constexpr float kBoundsEmpty = 1e30f;  // starting value of a min/max box (anything shrinks it)
constexpr float kRoomFogDensity = 0.075f, kOutdoorFogDensity = 0.040f;  // outdoors the air is clearer
constexpr double kMaxHandPitch = 1.5;  // radians: the lagging hand light never points quite straight up or down

// How far past the panel's own size the door's culling box reaches, in metres.
constexpr float kDoorCullMargin = 0.1f;

// Door hinge placement within its cell (see doorInstance): kHingeAcross is the hinge's offset on the
// axis across the passage; the other axis is kHingeBase, shifted by the swing side (-1 or 1).
constexpr double kHingeAcross = 0.175, kHingeBase = 0.5, kHingeSwingShift = 0.2;

// Mirror tuning (metres unless noted).
constexpr double kMirrorMinFront = 0.02;   // the eye must be this far in front of the glass to see it
constexpr float kMirrorClipOffset = 0.015f;  // the reflection keeps what is this far behind the plane, so the frame's own face drops out
constexpr float kMirrorGlassOffset = 0.004f;  // the glass quad floats this far off the prop's face (no z-fight)
constexpr int kMirrorMinSide = 16;  // texels; the reflection target never shrinks below this
// The reflection is drawn at three quarters of the main target's size.
constexpr int kMirrorScaleNum = 3, kMirrorScaleDen = 4;

// Motion blur: the smear is capped at this fraction of the picture (more for stronger settings).
constexpr double kMotionBlurCap = 0.08;

// The moon hangs over the house to the north, low enough to sit in view on the walk to the
// front door; the moonlight in the world shader comes from the same direction.
constexpr float kMoonDir[3] = {0.148f, 0.417f, -0.897f};

constexpr float kNormalStrength = 2.5f;  // how far a texture's brightness tilts its surface
constexpr float kChunk = 8.0f;  // cells per side of a culling chunk
constexpr GLsizei kVertexStride = 32;  // bytes: 3 floats position, 3 normal, 2 uv
constexpr int kNormalUnit = 6, kMaterialUnit = 7;  // texture units of a batch's normal and material maps
constexpr int kFirstShadowUnit = 1, kHandShadowUnit = 5;  // the lamps' cubes on 1..4, the hand light's on 5

// Binds the interleaved position / normal / uv layout of a Vertex to the bound VAO.
void setVertexLayout(GLuint vbo) {
    BindBuffer(ARRAY_BUFFER, vbo);
    EnableVertexAttribArray(0);
    VertexAttribPointer(0, 3, FLOAT, FALSE_, kVertexStride, nullptr);
    EnableVertexAttribArray(1);
    VertexAttribPointer(1, 3, FLOAT, FALSE_, kVertexStride, reinterpret_cast<const void*>(12));
    EnableVertexAttribArray(2);
    VertexAttribPointer(2, 2, FLOAT, FALSE_, kVertexStride, reinterpret_cast<const void*>(24));
}

void resetBounds(float* bmin, float* bmax) {
    for (int k = 0; k < 3; ++k) {
        bmin[k] = kBoundsEmpty;
        bmax[k] = -kBoundsEmpty;
    }
}

void growBounds(float* bmin, float* bmax, const float* otherMin, const float* otherMax) {
    for (int k = 0; k < 3; ++k) {
        bmin[k] = std::min(bmin[k], otherMin[k]);
        bmax[k] = std::max(bmax[k], otherMax[k]);
    }
}

}  // namespace

bool SceneRenderer::init(const TextureLibrary& textures) {
    // World and sky shaders call finishPixel() from the shared post-processing chunk.
    worldProgram_.build(shaders::kWorldGeometryVert, shaders::kWorldLightingFrag, {{0, "aPos"}, {1, "aNormal"}, {2, "aUV"}}, shaders::kToneMapAndGrainFrag);
    shadowProgram_.build(shaders::kWorldGeometryVert, shaders::kShadowDistanceFrag, {{0, "aPos"}, {1, "aNormal"}, {2, "aUV"}});
    compositeProgram_.build(shaders::kFullscreenQuadVert, shaders::kLensAndFxaaFrag, {{0, "aPos"}, {2, "aUV"}});
    quadProgram_.build(shaders::kFullscreenQuadVert, shaders::kOverlayFrag, {{0, "aPos"}, {2, "aUV"}});
    skyProgram_.build(shaders::kFullscreenQuadVert, shaders::kNightSkyFrag, {{0, "aPos"}, {2, "aUV"}}, shaders::kToneMapAndGrainFrag);
    mirrorProgram_.build(shaders::kMirrorGlassVert, shaders::kMirrorGlassFrag, {{0, "aPos"}});
    const bool textOk = text_.init();
    if (worldProgram_.valid()) lookUpWorldUniforms();
    buildFullscreenQuad();
    uploadTextures(textures);

    // The mirror: its glass is six vertices rewritten each frame; what it shows is drawn into mirrorTarget_.
    mirrorVbo_.upload(nullptr, 6 * sizeof(Vertex), DYNAMIC_DRAW);
    mirrorVao_.create();
    mirrorVao_.bind();
    setVertexLayout(mirrorVbo_.id());
    BindVertexArray(0);
    mirrorTarget_.create(kInternalW / 2, kInternalH / 2);

    // Overlay texture: re-uploaded every frame with nearest filtering.
    overlayTex_.create();
    overlayTex_.setParams(NEAREST, NEAREST, CLAMP_TO_EDGE);

    // Shadow maps are made on first use (ensureShadowMaps).

    // Internal scene target (its size is a graphics setting; see setRenderScale).
    target_.create(kInternalW, kInternalH);
    return worldProgram_.valid() && quadProgram_.valid() && compositeProgram_.valid() && skyProgram_.valid() && textOk;
}

// The world program's uniform locations (per-frame ones into world_u_) and its fixed sampler units.
void SceneRenderer::lookUpWorldUniforms() {
    uModel_ = worldProgram_.uniform("uModel");
    uEmissive_ = worldProgram_.uniform("uEmissive");
    uAlphaTest_ = worldProgram_.uniform("uAlphaTest");
    WorldUniforms& u = world_u_;
    u.view = worldProgram_.uniform("uView");
    u.proj = worldProgram_.uniform("uProj");
    u.cam = worldProgram_.uniform("uCam");
    u.flashDir = worldProgram_.uniform("uFlashDir");
    u.flashOn = worldProgram_.uniform("uFlashOn");
    u.lighter = worldProgram_.uniform("uLighter");
    u.outdoor = worldProgram_.uniform("uOutdoor");
    u.moonDir = worldProgram_.uniform("uMoonDir");
    u.fogColor = worldProgram_.uniform("uFogColor");
    u.fogDensity = worldProgram_.uniform("uFogDensity");
    u.time = worldProgram_.uniform("uTime");
    u.ambientBase = worldProgram_.uniform("uAmbientBase");
    u.ambientNear = worldProgram_.uniform("uAmbientNear");
    u.lightCount = worldProgram_.uniform("uLightCount");
    u.lightPos = worldProgram_.uniform("uLightPos[0]");
    u.lightColor = worldProgram_.uniform("uLightColor[0]");
    u.lightRadius = worldProgram_.uniform("uLightRadius[0]");
    u.shadowCount = worldProgram_.uniform("uShadowCount");
    u.shadowTexel = worldProgram_.uniform("uShadowTexel");
    u.handShadow = worldProgram_.uniform("uHandShadow");
    u.handPos = worldProgram_.uniform("uHandPos");
    u.handFaces = worldProgram_.uniform("uHandFaces");
    u.res = worldProgram_.uniform("uRes");
    u.grain = worldProgram_.uniform("uGrain");
    u.materials = worldProgram_.uniform("uMaterials");
    u.clip = worldProgram_.uniform("uClip");
    // The samplers never change: the texture on unit 0, the shadow cubes on units 1 to 5.
    worldProgram_.use();
    worldProgram_.set("uTex", 0);
    worldProgram_.set("uShadow0", 1);
    worldProgram_.set("uShadow1", 2);
    worldProgram_.set("uShadow2", 3);
    worldProgram_.set("uShadow3", 4);
    worldProgram_.set("uShadowHand", 5);
    worldProgram_.set("uNormalTex", 6);
    worldProgram_.set("uMaterialTex", 7);
}

void SceneRenderer::buildFullscreenQuad() {
    // Fullscreen quad: position (x, y) and uv per vertex, two triangles.
    const float quad[24] = {
        -1, -1, 0, 0,  1, -1, 1, 0,  1, 1, 1, 1,
        -1, -1, 0, 0,  1,  1, 1, 1, -1, 1, 0, 1,
    };
    quadVbo_.upload(quad, sizeof(quad), STATIC_DRAW);
    quadVao_.create();
    quadVao_.bind();
    BindBuffer(ARRAY_BUFFER, quadVbo_.id());
    EnableVertexAttribArray(0);
    VertexAttribPointer(0, 2, FLOAT, FALSE_, 16, nullptr);
    EnableVertexAttribArray(2);
    VertexAttribPointer(2, 2, FLOAT, FALSE_, 16, reinterpret_cast<const void*>(8));
    BindVertexArray(0);
}

// Uploads a surface as a mipmapped, repeating texture (internalFormat: sRGB for colour, linear for data maps).
GlTexture SceneRenderer::makeMipmappedTexture(int w, int h, const void* rgba, gl::GLenum internalFormat) {
    GlTexture tex;
    tex.create();
    tex.allocate(w, h, rgba, internalFormat);
    GenerateMipmap(TEXTURE_2D);
    tex.setParams(LINEAR_MIPMAP_LINEAR, LINEAR, REPEAT);
    return tex;
}

void SceneRenderer::uploadTextures(const TextureLibrary& textures) {
    // Every texture the mesh builder can reference. GL texture row 0 sits at v = 0 (the
    // bottom of a wall), and Surface row 0 is the top of the image, so flip while converting.
    std::vector<uint8_t> rgbaBytes;
    for (const auto& name : textures.names()) {
        if (isPbrCompanion(name)) {
            // A companion map is read with its texture below; one with no texture is drawn as itself.
            const std::string base = name.substr(0, name.rfind('_'));
            if (textures.has(base)) continue;
        }
        const Surface& s = textures.get(name);
        surfaceToRgbaFlipped(s, rgbaBytes);
        textures_.emplace_back(name, makeMipmappedTexture(s.w, s.h, rgbaBytes.data(), SRGB8_ALPHA8));

        // Its normal map (alpha: gloss). Authored maps (<name>_nrm, <name>_rough) win; otherwise the normal is
        // read from the texture's own brightness and there is no specular (gfx/pbr.h).
        const std::string nrmName = name + kNormalSuffix, roughName = name + kRoughSuffix,
                          metalName = name + kMetalSuffix, aoName = name + kAoSuffix;
        surfaceToRgbaFlipped(makeSurfaceMap(s, textures.has(nrmName) ? &textures.get(nrmName) : nullptr,
                                            textures.has(roughName) ? &textures.get(roughName) : nullptr, kNormalStrength),
                             rgbaBytes);
        normalMaps_.emplace_back(name, makeMipmappedTexture(s.w, s.h, rgbaBytes.data()));

        // Its material map (R metal, G occlusion), only when it has a companion for one of them.
        const Surface mat = makeMaterialMap(s, textures.has(metalName) ? &textures.get(metalName) : nullptr,
                                            textures.has(aoName) ? &textures.get(aoName) : nullptr);
        if (!mat.empty()) {
            surfaceToRgbaFlipped(mat, rgbaBytes);
            materialMaps_.emplace_back(name, makeMipmappedTexture(mat.w, mat.h, rgbaBytes.data()));
        }
    }
    {
        const uint8_t open[4] = {0, 255, 255, 255};  // metal 0, occlusion 1
        defaultMaterial_.create();
        defaultMaterial_.allocate(1, 1, open);
        defaultMaterial_.setParams(NEAREST, NEAREST, REPEAT);
    }
}

void SceneRenderer::setRenderScale(int preset) { renderScale_ = std::clamp(preset, 0, kRenderScaleCount - 1); }

void SceneRenderer::resizeTarget(int windowW, int windowH) {
    if (windowW <= 0 || windowH <= 0) return;
    const int h = kRenderScaleLines[renderScale_];
    const int w = std::max(1, static_cast<int>(std::lround(static_cast<double>(h) * windowW / windowH)));
    target_.resize(w, h);
}

GLuint SceneRenderer::textureFor(const std::string& name) const {
    for (const auto& t : textures_)
        if (t.first == name) return t.second.id();
    for (const auto& t : textures_)
        if (t.first == "missing") return t.second.id();
    return 0;
}

GLuint SceneRenderer::normalMapFor(const std::string& name) const {
    for (const auto& t : normalMaps_)
        if (t.first == name) return t.second.id();
    for (const auto& t : normalMaps_)
        if (t.first == "missing") return t.second.id();
    return 0;
}

GLuint SceneRenderer::materialMapFor(const std::string& name) const {
    for (const auto& t : materialMaps_)
        if (t.first == name) return t.second.id();
    return defaultMaterial_.id();
}

void SceneRenderer::uploadBatches(BatchList& dst, const std::vector<Batch>& src) {
    for (const auto& b : src) {
        if (b.verts.empty()) continue;
        // Bucket whole triangles by the chunk holding their first vertex; the ordered map then
        // lays the chunks out one after another in a single buffer.
        std::map<std::pair<int, int>, std::vector<Vertex>> chunks;
        for (size_t i = 0; i + 2 < b.verts.size(); i += 3) {
            const Vertex& v = b.verts[i];
            auto& list = chunks[{static_cast<int>(std::floor(v.px / kChunk)), static_cast<int>(std::floor(v.pz / kChunk))}];
            list.insert(list.end(), b.verts.begin() + static_cast<std::ptrdiff_t>(i),
                        b.verts.begin() + static_cast<std::ptrdiff_t>(i) + 3);
        }
        GpuBatch g;
        std::vector<Vertex> all;
        all.reserve(b.verts.size());
        resetBounds(g.bmin, g.bmax);
        for (const auto& [key, verts] : chunks) {
            GpuChunk c;
            c.first = static_cast<int>(all.size());
            c.count = static_cast<int>(verts.size());
            resetBounds(c.bmin, c.bmax);
            for (const Vertex& v : verts) {
                const float p[3] = {v.px, v.py, v.pz};
                growBounds(c.bmin, c.bmax, p, p);
            }
            growBounds(g.bmin, g.bmax, c.bmin, c.bmax);
            all.insert(all.end(), verts.begin(), verts.end());
            g.chunks.push_back(c);
        }
        g.vbo.upload(all.data(), all.size() * sizeof(Vertex), STATIC_DRAW);
        g.vao.create();
        g.vao.bind();
        setVertexLayout(g.vbo.id());
        BindVertexArray(0);
        g.texture = textureFor(b.texture);
        g.normal = normalMapFor(b.texture);
        g.material = materialMapFor(b.texture);
        g.emissive = b.emissive;
        g.alphaTest = b.alphaTest;
        dst.push_back(std::move(g));
    }
}

int SceneRenderer::uploadModel(const std::vector<Batch>& batches) {
    ++sceneRevision_;
    models_.emplace_back();
    uploadBatches(models_.back(), batches);
    return static_cast<int>(models_.size()) - 1;
}

void SceneRenderer::uploadWorld(const SceneGeometry& geometry) {
    ++sceneRevision_;
    world_.clear();
    props_.clear();
    uploadBatches(world_, geometry.world);
    uploadBatches(props_, geometry.props);
}

void SceneRenderer::uploadCab(const SceneGeometry& geometry) {
    ++sceneRevision_;
    cab_.clear();
    uploadBatches(cab_, geometry.world);
    uploadBatches(cab_, geometry.props);
}

void SceneRenderer::uploadDoorPanel(const std::vector<Batch>& batches) {
    ++sceneRevision_;
    door_.clear();
    uploadBatches(door_, batches);
}

// Binds a batch's textures, flags and VAO, skipping whatever drawState_ says is already set.
void SceneRenderer::bindBatchState(const GpuBatch& b) {
    if (b.texture != drawState_.texture) {
        BindTexture(TEXTURE_2D, b.texture);
        drawState_.texture = b.texture;
    }
    if (bindNormals_ && (b.normal != drawState_.normal || b.material != drawState_.material)) {
        ActiveTexture(TEXTURE0 + kNormalUnit);
        BindTexture(TEXTURE_2D, b.normal);
        ActiveTexture(TEXTURE0 + kMaterialUnit);
        BindTexture(TEXTURE_2D, b.material);
        ActiveTexture(TEXTURE0);
        drawState_.normal = b.normal;
        drawState_.material = b.material;
    }
    const int emissive = b.emissive ? 1 : 0, alphaTest = b.alphaTest ? 1 : 0;
    if (emissive != drawState_.emissive) {
        Uniform1i(uEmissive_, emissive);
        drawState_.emissive = emissive;
    }
    if (alphaTest != drawState_.alphaTest) {
        Uniform1i(uAlphaTest_, alphaTest);
        drawState_.alphaTest = alphaTest;
    }
    b.vao.bind();
}

void SceneRenderer::drawBatches(const BatchList& batches, const float* model, const Frustum* cull,
                                float offsetZ) {
    UniformMatrix4fv(uModel_, 1, FALSE_, model);
    for (const auto& b : batches) {
        if (cull && !boxInFrustum(*cull, b.bmin, b.bmax, offsetZ)) continue;
        bool bound = false;  // state is set up when the first chunk turns out to be visible
        // Neighbouring visible chunks are one range of the buffer: they go out as a single draw.
        int runFirst = 0, runCount = 0;
        auto flush = [&] {
            if (runCount > 0) DrawArrays(TRIANGLES, runFirst, runCount);
            runCount = 0;
        };
        for (const auto& c : b.chunks) {
            if (cull && !boxInFrustum(*cull, c.bmin, c.bmax, offsetZ)) {
                flush();
                continue;
            }
            if (!bound) {
                bound = true;
                bindBatchState(b);
            }
            if (runCount > 0 && runFirst + runCount == c.first) {
                runCount += c.count;
            } else {
                flush();
                runFirst = c.first;
                runCount = c.count;
            }
        }
        flush();
    }
}

void SceneRenderer::drawQuad() {
    quadVao_.bind();
    DrawArrays(TRIANGLES, 0, 6);
}

// Sky first, outdoors: a fullscreen pass with depth off (so it stays behind everything).
void SceneRenderer::drawSky(const CameraState& cam, const Frame& f) {
    Disable(DEPTH_TEST);
    skyProgram_.use();
    // The camera basis, so the shader can turn a pixel into a view ray.
    const float cosPitch = std::cos(static_cast<float>(cam.pitch)), sinPitch = std::sin(static_cast<float>(cam.pitch));
    const float cosYaw = std::cos(static_cast<float>(cam.yaw)), sinYaw = std::sin(static_cast<float>(cam.yaw));
    skyProgram_.set("uFwd", cosYaw * cosPitch, sinPitch, sinYaw * cosPitch);
    skyProgram_.set("uRight", -sinYaw, 0.0f, cosYaw);
    skyProgram_.set("uUp", -cosYaw * sinPitch, cosPitch, -sinYaw * sinPitch);
    const float tanHalfY = std::tan(f.fovY * 0.5f);
    skyProgram_.set("uTan", tanHalfY * f.aspect, tanHalfY);
    skyProgram_.set("uTime", static_cast<float>(cam.time));
    skyProgram_.set("uMoonDir", f.moon[0], f.moon[1], f.moon[2]);
    skyProgram_.set("uFogColor", f.fog[0], f.fog[1], f.fog[2]);
    skyProgram_.set("uRes", static_cast<float>(target_.width()), static_cast<float>(target_.height()));
    skyProgram_.set("uGrain", f.grain);
    drawQuad();
}

// This frame's lamps (lamps_, shadow casters first) as the world shader's arrays.
void SceneRenderer::setLampUniforms() {
    const WorldUniforms& u = world_u_;
    const int n = static_cast<int>(lamps_.size());
    float pos[CameraState::kMaxLights * 3], col[CameraState::kMaxLights * 3], rad[CameraState::kMaxLights];
    for (int i = 0; i < n; ++i) {
        const PointLight& l = lamps_[static_cast<size_t>(i)];
        pos[i * 3] = static_cast<float>(l.x);
        pos[i * 3 + 1] = static_cast<float>(l.y);
        pos[i * 3 + 2] = static_cast<float>(l.z);
        col[i * 3] = static_cast<float>(l.r);
        col[i * 3 + 1] = static_cast<float>(l.g);
        col[i * 3 + 2] = static_cast<float>(l.b);
        rad[i] = static_cast<float>(std::max(0.001, l.radius));  // the shader divides by it
    }
    Uniform1i(u.lightCount, n);
    if (n > 0) {
        Uniform3fv(u.lightPos, n, pos);
        Uniform3fv(u.lightColor, n, col);
        Uniform1fv(u.lightRadius, n, rad);
    }
}

// The lamps' shadow cubes on units 1..4 and the hand light's on unit 5. The active unit is left
// on 5; drawWorld resets it to 0 after the remaining uniforms.
void SceneRenderer::bindShadowCubes() {
    for (int i = 0; i < CameraState::kMaxShadowLights; ++i) {
        ActiveTexture(TEXTURE0 + static_cast<GLenum>(kFirstShadowUnit + i));
        BindTexture(TEXTURE_CUBE_MAP, shadow_[i].texture());
    }
    ActiveTexture(TEXTURE0 + kHandShadowUnit);
    BindTexture(TEXTURE_CUBE_MAP, handShadow_.texture());
}

void SceneRenderer::drawWorld(const CameraState& cam, const Frame& f) {
    Enable(DEPTH_TEST);
    DepthFunc(LEQUAL);

    worldProgram_.use();
    const WorldUniforms& u = world_u_;
    UniformMatrix4fv(u.view, 1, FALSE_, f.view);
    UniformMatrix4fv(u.proj, 1, FALSE_, f.proj);
    Uniform3f(u.cam, f.eye[0], f.eye[1], f.eye[2]);
    Uniform3f(u.flashDir, f.flash[0], f.flash[1], f.flash[2]);
    Uniform1f(u.flashOn, cam.flashOn ? 1.0f : 0.0f);
    Uniform1f(u.lighter, cam.lighter ? 1.0f : 0.0f);
    Uniform1i(u.outdoor, cam.outdoor ? 1 : 0);
    Uniform3f(u.moonDir, f.moon[0], f.moon[1], f.moon[2]);
    Uniform3f(u.fogColor, f.fog[0], f.fog[1], f.fog[2]);
    Uniform1f(u.fogDensity, f.fogDensity);
    Uniform1f(u.time, static_cast<float>(cam.time));
    Uniform3f(u.ambientBase, cam.ambientBase[0], cam.ambientBase[1], cam.ambientBase[2]);
    Uniform3f(u.ambientNear, cam.ambientNear[0], cam.ambientNear[1], cam.ambientNear[2]);
    setLampUniforms();
    Uniform1i(u.shadowCount, shadowCount_);
    Uniform1f(u.shadowTexel, 2.0f / static_cast<float>(shadowSize_));
    Uniform1i(u.handShadow, handShadowOn_ ? 1 : 0);
    Uniform3f(u.handPos, handPos_[0], handPos_[1], handPos_[2]);
    Uniform1i(u.handFaces, static_cast<int>(handFaces_));
    bindShadowCubes();
    Uniform2f(u.res, static_cast<float>(target_.width()), static_cast<float>(target_.height()));
    Uniform1f(u.grain, f.grain);
    Uniform1i(u.materials, f.materials);
    Uniform4f(u.clip, f.clip[0], f.clip[1], f.clip[2], f.clip[3]);
    ActiveTexture(TEXTURE0);
    drawState_ = DrawState{};

    drawScene(cam, f.planes, f.views);
}

// Everything that stands in the world this frame, culled against the planes: what the main pass
// draws and what casts shadows.
void SceneRenderer::drawScene(const CameraState& cam, const Frustum& cull, unsigned views) {
    float model[16];
    mat4Identity(model);
    drawBatches(cab_, model, &cull);
    model[14] = static_cast<float>(cam.scrollZ);
    drawBatches(world_, model, &cull, static_cast<float>(cam.scrollZ));
    drawBatches(props_, model, &cull, static_cast<float>(cam.scrollZ));
    drawDoors(cam, cull, model);
    if (views == 0) return;
    drawInstances(cam, views);
}

// The swinging door panels inside the frustum. `model` is scratch space for each panel's matrix.
void SceneRenderer::drawDoors(const CameraState& cam, const Frustum& cull, float* model) {
    for (const auto& door : cam.doors) {
        // The panel swings within kDoorPanelWidth of its hinge.
        const float hingeX = static_cast<float>(door.hingeX), hingeZ = static_cast<float>(door.hingeZ);
        const float reach = kDoorPanelWidth + kDoorCullMargin;
        const float bmin[3] = {hingeX - reach, 0.0f, hingeZ - reach};
        const float bmax[3] = {hingeX + reach, kDoorPanelHeight + kDoorCullMargin, hingeZ + reach};
        if (!boxInFrustum(cull, bmin, bmax)) continue;
        doorModel(door.hingeX, door.hingeZ, door.angle, model);
        drawBatches(door_, model);
    }
}

// The caller's moving models (the hand light, the player's body) that show in these views.
void SceneRenderer::drawInstances(const CameraState& cam, unsigned views) {
    for (const ModelInstance& inst : cam.instances) {
        if ((inst.views & views) == 0 || inst.model < 0 || inst.model >= static_cast<int>(models_.size())) continue;
        drawBatches(models_[static_cast<size_t>(inst.model)], inst.matrix);
    }
}

// Whether the glass can be seen from here: the eye is on its front, and it is inside the view.
bool SceneRenderer::mirrorVisible(const CameraState& cam, const Frame& f) const {
    const MirrorState& m = cam.mirror;
    if (!m.on || !mirrorProgram_.valid() || !mirrorTarget_.width()) return false;
    const double front = (cam.x - m.x) * m.nx + (cam.z - m.z) * m.nz;
    if (front < kMirrorMinFront) return false;
    const double radius = std::hypot(m.halfW, m.halfH);  // a box around the glass, whatever its facing
    const float bmin[3] = {static_cast<float>(m.x - radius), static_cast<float>(m.y - radius), static_cast<float>(m.z - radius)};
    const float bmax[3] = {static_cast<float>(m.x + radius), static_cast<float>(m.y + radius), static_cast<float>(m.z + radius)};
    return boxInFrustum(f.planes, bmin, bmax);
}

// The scene as the glass shows it: the view turned about the mirror's plane (so the picture is the room
// reflected, drawn with the real lights), without what lies behind the glass, into mirrorTarget_.
void SceneRenderer::drawMirrorView(const CameraState& cam, const Frame& f) {
    const MirrorState& m = cam.mirror;
    const int h = std::max(kMirrorMinSide, target_.height() * kMirrorScaleNum / kMirrorScaleDen);
    const int w = std::max(kMirrorMinSide, target_.width() * kMirrorScaleNum / kMirrorScaleDen);
    mirrorTarget_.resize(w, h);
    const float n[3] = {static_cast<float>(m.nx), 0.0f, static_cast<float>(m.nz)};
    const float d = n[0] * static_cast<float>(m.x) + n[2] * static_cast<float>(m.z);  // the plane: n.p = d
    float reflect[16];  // p' = p - 2 (n.p - d) n
    mat4Identity(reflect);
    reflect[0] = 1 - 2 * n[0] * n[0];
    reflect[2] = -2 * n[0] * n[2];
    reflect[8] = -2 * n[2] * n[0];
    reflect[10] = 1 - 2 * n[2] * n[2];
    reflect[12] = 2 * d * n[0];
    reflect[14] = 2 * d * n[2];
    Frame fm = f;
    mat4Multiply(f.view, reflect, fm.view);
    float viewProj[16];
    mat4Multiply(f.proj, fm.view, viewProj);
    frustumPlanes(viewProj, fm.planes);
    // Keep what is in front of the glass: the frame and the picture of the mirror prop itself (which lie on the plane)
    // and the wall it hangs on go, or they would hide the whole reflection.
    fm.clip[0] = n[0];
    fm.clip[1] = 0.0f;
    fm.clip[2] = n[2];
    fm.clip[3] = -d - kMirrorClipOffset;
    fm.views = ModelInstance::kMirror;
    mirrorTarget_.bind();
    Viewport(0, 0, w, h);
    ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    Clear(COLOR_BUFFER_BIT | DEPTH_BUFFER_BIT);
    Disable(BLEND);
    drawWorld(cam, fm);
}

// The glass: a quad a finger in front of the mirror prop's own face, showing what drawMirrorView made.
void SceneRenderer::drawMirrorGlass(const CameraState& cam, const Frame& f) {
    const MirrorState& m = cam.mirror;
    const float nx = static_cast<float>(m.nx), nz = static_cast<float>(m.nz);
    const float alongX = nz, alongZ = -nx;  // the glass's horizontal axis
    const float centerX = static_cast<float>(m.x) + nx * kMirrorGlassOffset;
    const float centerZ = static_cast<float>(m.z) + nz * kMirrorGlassOffset;
    const float halfW = static_cast<float>(m.halfW), halfH = static_cast<float>(m.halfH);
    const float centerY = static_cast<float>(m.y);
    auto corner = [&](float sx, float sy) {  // sx, sy: -1 or 1, the corner's side
        Vertex v{};
        v.px = centerX + alongX * halfW * sx;
        v.py = centerY + halfH * sy;
        v.pz = centerZ + alongZ * halfW * sx;
        v.nx = nx;
        v.nz = nz;
        return v;
    };
    const Vertex quad[6] = {corner(-1, -1), corner(1, -1), corner(1, 1), corner(-1, -1), corner(1, 1), corner(-1, 1)};
    mirrorVbo_.upload(quad, sizeof(quad), DYNAMIC_DRAW);
    mirrorProgram_.use();
    UniformMatrix4fv(mirrorProgram_.uniform("uView"), 1, FALSE_, f.view);
    UniformMatrix4fv(mirrorProgram_.uniform("uProj"), 1, FALSE_, f.proj);
    mirrorProgram_.set("uTex", 0);
    mirrorProgram_.set("uRes", static_cast<float>(target_.width()), static_cast<float>(target_.height()));
    mirrorProgram_.set("uTint", static_cast<float>(m.tint[0]), static_cast<float>(m.tint[1]), static_cast<float>(m.tint[2]));
    mirrorProgram_.set("uAlpha", static_cast<float>(m.alpha));
    ActiveTexture(TEXTURE0);
    mirrorTarget_.color().bind();
    Enable(DEPTH_TEST);
    DepthFunc(LEQUAL);
    Enable(BLEND);
    BlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
    mirrorVao_.bind();
    DrawArrays(TRIANGLES, 0, 6);
    Disable(BLEND);
}

// Motion blur: the turn since the last frame, as a distance across the picture. A turn of
// the view moves everything the same way, so a straight smear along it suffices (the sign
// does not matter: the taps straddle each pixel). Long smears are capped. Remembers this
// frame's view for the next call.
void SceneRenderer::motionBlurSmear(const CameraState& cam, const Frame& f, float& smearX, float& smearY) {
    smearX = smearY = 0.0f;
    if (cam.motionBlur > 0.0 && haveLastView_) {
        double dyaw = cam.yaw - lastYaw_;
        dyaw = std::remainder(dyaw, kTwoPi);
        const double dpitch = cam.pitch - lastPitch_;
        const double fovX = 2.0 * std::atan(std::tan(f.fovY * 0.5) * f.aspect);
        // The strength scales the smear; the cap grows with it so the strongest still reads.
        const double cap = kMotionBlurCap * std::max(1.0, cam.motionBlur);
        smearX = static_cast<float>(std::clamp(dyaw / fovX * cam.motionBlur, -cap, cap));
        smearY = static_cast<float>(std::clamp(dpitch / f.fovY * cam.motionBlur, -cap, cap));
    }
    lastYaw_ = cam.yaw;
    lastPitch_ = cam.pitch;
    haveLastView_ = true;
}

// Composite the scene to the window (linear upscale keeps it soft, PS2-like), with the blur and lens.
void SceneRenderer::composite(const CameraState& cam, const Frame& f, int windowW, int windowH) {
    RenderTarget::bindWindow();
    Viewport(0, 0, windowW, windowH);
    ClearColor(0, 0, 0, 1);
    Clear(COLOR_BUFFER_BIT | DEPTH_BUFFER_BIT);
    Disable(DEPTH_TEST);
    Disable(BLEND);

    float bx = 0.0f, by = 0.0f;
    motionBlurSmear(cam, f, bx, by);

    compositeProgram_.use();
    ActiveTexture(TEXTURE0);
    target_.color().bind();
    compositeProgram_.set("uTex", 0);
    compositeProgram_.set("uBlur", bx, by);
    compositeProgram_.set("uLens", cam.lensEffect ? 1.0f : 0.0f);
    // FXAA only on a still picture: a blurred one has no edges to smooth.
    compositeProgram_.set("uFxaa", cam.antiAlias == 1 && bx == 0.0f && by == 0.0f ? 1.0f : 0.0f);
    compositeProgram_.set("uTexel", 1.0f / static_cast<float>(target_.width()), 1.0f / static_cast<float>(target_.height()));
    drawQuad();
}

// Uploads the overlay as it is (0xAARRGGBB read as BGRA 8-8-8-8 reversed; the shader flips it
// because row 0 is the top). A frame identical to the last one is not uploaded again.
void SceneRenderer::uploadOverlay(const Surface& overlay) {
    overlayTex_.bind();
    const bool sameSize = overlay.w == overlayW_ && overlay.h == overlayH_;
    if (sameSize && overlay.px == overlayPixels_) return;
    if (sameSize) {
        TexSubImage2D(TEXTURE_2D, 0, 0, 0, overlay.w, overlay.h, BGRA, UNSIGNED_INT_8_8_8_8_REV, overlay.px.data());
    } else {
        TexImage2D(TEXTURE_2D, 0, RGBA8, overlay.w, overlay.h, 0, BGRA, UNSIGNED_INT_8_8_8_8_REV, overlay.px.data());
        overlayW_ = overlay.w;
        overlayH_ = overlay.h;
    }
    overlayPixels_ = overlay.px;
}

// The 2D overlay with alpha blending, then its text at full window resolution.
void SceneRenderer::drawOverlay(const Surface& overlay, const TextLayer* text) {
    uploadOverlay(overlay);
    quadProgram_.use();
    quadProgram_.set("uFlipY", 1.0f);
    ActiveTexture(TEXTURE0);
    overlayTex_.bind();
    Enable(BLEND);
    BlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
    drawQuad();
    Disable(BLEND);
    quadProgram_.set("uFlipY", 0.0f);
    if (text && !text->runs.empty()) text_.draw(*text, overlay.w, overlay.h);
}

// Everything the frame's passes share: the eye, the hand light's aim, fog, and the view
// and projection matrices with the frustum they imply. Needs target_ at this frame's size.
SceneRenderer::Frame SceneRenderer::makeFrame(const CameraState& cam) const {
    Frame f;
    f.eye[0] = static_cast<float>(cam.x);
    f.eye[1] = static_cast<float>(cam.eyeHeight);
    f.eye[2] = static_cast<float>(cam.z);
    f.dir[0] = static_cast<float>(std::cos(cam.yaw) * std::cos(cam.pitch));
    f.dir[1] = static_cast<float>(std::sin(cam.pitch));
    f.dir[2] = static_cast<float>(std::sin(cam.yaw) * std::cos(cam.pitch));
    {   // the hand light's own aim: the view's, turned by the hand's lag
        const double flashYaw = cam.yaw + cam.handYaw;
        const double flashPitch = std::clamp(cam.pitch + cam.handPitch, -kMaxHandPitch, kMaxHandPitch);
        f.flash[0] = static_cast<float>(std::cos(flashYaw) * std::cos(flashPitch));
        f.flash[1] = static_cast<float>(std::sin(flashPitch));
        f.flash[2] = static_cast<float>(std::sin(flashYaw) * std::cos(flashPitch));
    }
    std::copy(std::begin(kMoonDir), std::end(kMoonDir), f.moon);
    // Fog: a cold blue night outdoors, a near-black warm murk indoors (linear-space colours).
    f.fog[0] = cam.outdoor ? 0.012f : 0.006f;
    f.fog[1] = cam.outdoor ? 0.018f : 0.005f;
    f.fog[2] = cam.outdoor ? 0.034f : 0.005f;
    f.fogDensity = cam.outdoor ? kOutdoorFogDensity : kRoomFogDensity;
    f.fovY = kFovY;
    f.views = ModelInstance::kMain;
    f.aspect = static_cast<float>(target_.width()) / static_cast<float>(target_.height());
    f.grain = cam.grain ? 1.0f : 0.0f;
    f.materials = std::clamp(cam.materials, 0, 2);
    const float center[3] = {f.eye[0] + f.dir[0], f.eye[1] + f.dir[1], f.eye[2] + f.dir[2]};
    const float up[3] = {0, 1, 0};
    mat4LookAt(f.eye, center, up, f.view);
    mat4Perspective(f.fovY, f.aspect, kNearPlane, kFarPlane, f.proj);
    float viewProj[16];
    mat4Multiply(f.proj, f.view, viewProj);
    frustumPlanes(viewProj, f.planes);
    return f;
}

// The player's view into the internal target: sky outdoors, then the world, then the mirror glass.
void SceneRenderer::drawMainView(const CameraState& cam, const Frame& f, bool mirrored) {
    target_.bind();
    Viewport(0, 0, target_.width(), target_.height());
    ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    Clear(COLOR_BUFFER_BIT | DEPTH_BUFFER_BIT);
    Disable(BLEND);
    if (cam.outdoor) drawSky(cam, f);
    drawWorld(cam, f);
    if (mirrored) drawMirrorGlass(cam, f);
    target_.resolve();  // a multisampled picture becomes the texture the composite reads
}

void SceneRenderer::render(const CameraState& cam, int windowW, int windowH, const Surface* overlay,
                           const TextLayer* text) {
    target_.setSamples(kAntiAliasSamples[std::clamp(cam.antiAlias, 0, kAntiAliasCount - 1)]);
    resizeTarget(windowW, windowH);
    const Frame f = makeFrame(cam);

    // 1) The lamps' shadow maps, 2) the world into the internal target.
    chooseLamps(cam, f);
    drawShadows(cam);
    const bool mirrored = mirrorVisible(cam, f);
    if (mirrored) drawMirrorView(cam, f);  // before the main pass: it has its own target
    drawMainView(cam, f, mirrored);

    // 3) The scene to the window, 4) the 2D overlay on top.
    composite(cam, f, windowW, windowH);
    if (overlay && !overlay->empty()) drawOverlay(*overlay, text);
}

DoorInstance doorInstance(const DoorState& d) {
    const double eased = d.open * d.open * (3.0 - 2.0 * d.open);  // smoothstep: ease the swing
    const double swing = kHalfPi * eased;
    const double along = kHingeBase + kHingeSwingShift * d.swing;  // the post face it swings past
    DoorInstance di;
    if (d.passageAlongX) {  // shut, the panel runs south from the north post
        di.hingeX = d.x + along;
        di.hingeZ = d.y + kHingeAcross;
        di.angle = kHalfPi - d.swing * swing;
    } else {  // shut, the panel runs east from the west post
        di.hingeX = d.x + kHingeAcross;
        di.hingeZ = d.y + along;
        di.angle = d.swing * swing;
    }
    return di;
}

}  // namespace mythos
