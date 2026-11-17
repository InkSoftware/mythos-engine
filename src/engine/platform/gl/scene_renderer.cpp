#include "engine/platform/gl/scene_renderer.h"

#include "engine/core/mat4.h"
#include "engine/gfx/normalmap.h"
#include "engine/platform/gl/scene_shaders.h"
#include "engine/ui/settings.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>

namespace mythos {
namespace {

using namespace gl;

// Door panel hinged at (hx, hz), its spine turned to absolute angle a on the map plane.
void doorModel(double hx, double hz, double a, float* m) {
    const float c = static_cast<float>(std::cos(a)), s = static_cast<float>(std::sin(a));
    mat4Identity(m);
    m[0] = c;   m[2] = s;   // local +x (the spine) -> (cos a, 0, sin a)
    m[8] = -s;  m[10] = c;  // local +z (the thickness) stays perpendicular
    m[12] = static_cast<float>(hx);
    m[14] = static_cast<float>(hz);
}

constexpr float kFovY = 62.0f * 3.14159265f / 180.0f;

// The moon hangs over the house to the north, low enough to sit in view on the walk to the
// front door; the moonlight in the world shader comes from the same direction.
constexpr float kMoonDir[3] = {0.148f, 0.417f, -0.897f};

constexpr float kNormalStrength = 2.5f;  // how far a texture's brightness tilts its surface
constexpr float kChunk = 8.0f;  // cells per side of a culling chunk

// Binds the interleaved position / normal / uv layout of a Vertex to the bound VAO.
void setVertexLayout(GLuint vbo) {
    BindBuffer(ARRAY_BUFFER, vbo);
    EnableVertexAttribArray(0);
    VertexAttribPointer(0, 3, FLOAT, FALSE_, 32, nullptr);
    EnableVertexAttribArray(1);
    VertexAttribPointer(1, 3, FLOAT, FALSE_, 32, reinterpret_cast<const void*>(12));
    EnableVertexAttribArray(2);
    VertexAttribPointer(2, 2, FLOAT, FALSE_, 32, reinterpret_cast<const void*>(24));
}

}  // namespace

bool SceneRenderer::init(const TextureLibrary& textures) {
    // World and sky shaders call finishPixel() from the shared post-processing chunk.
    worldProgram_.build(shaders::kWorldGeometryVert, shaders::kWorldLightingFrag, {{0, "aPos"}, {1, "aNormal"}, {2, "aUV"}}, shaders::kToneMapAndGrainFrag);
    shadowProgram_.build(shaders::kWorldGeometryVert, shaders::kShadowDistanceFrag, {{0, "aPos"}, {1, "aNormal"}, {2, "aUV"}});
    compositeProgram_.build(shaders::kFullscreenQuadVert, shaders::kLensAndFxaaFrag, {{0, "aPos"}, {2, "aUV"}});
    quadProgram_.build(shaders::kFullscreenQuadVert, shaders::kOverlayFrag, {{0, "aPos"}, {2, "aUV"}});
    skyProgram_.build(shaders::kFullscreenQuadVert, shaders::kNightSkyFrag, {{0, "aPos"}, {2, "aUV"}}, shaders::kToneMapAndGrainFrag);
    const bool textOk = text_.init();
    if (worldProgram_.valid()) {
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
        u.res = worldProgram_.uniform("uRes");
        u.grain = worldProgram_.uniform("uGrain");
        // The samplers never change: the texture on unit 0, the shadow cubes on units 1 to 5.
        worldProgram_.use();
        worldProgram_.set("uTex", 0);
        worldProgram_.set("uShadow0", 1);
        worldProgram_.set("uShadow1", 2);
        worldProgram_.set("uShadow2", 3);
        worldProgram_.set("uShadow3", 4);
        worldProgram_.set("uShadowHand", 5);
        worldProgram_.set("uNormalTex", 6);
    }

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

    // Every texture the mesh builder can reference. GL texture row 0 sits at v = 0 (the
    // bottom of a wall), and Surface row 0 is the top of the image, so flip while converting.
    std::vector<uint8_t> rgbaBytes;
    for (const auto& name : textures.names()) {
        const Surface& s = textures.get(name);
        surfaceToRgbaFlipped(s, rgbaBytes);
        GlTexture tex;
        tex.create();
        tex.allocate(s.w, s.h, rgbaBytes.data());
        GenerateMipmap(TEXTURE_2D);
        tex.setParams(LINEAR_MIPMAP_LINEAR, LINEAR, REPEAT);
        textures_.emplace_back(name, std::move(tex));

        // Its normal map, read from the texture's own brightness (the art has none by hand).
        surfaceToRgbaFlipped(makeNormalMap(s, kNormalStrength), rgbaBytes);
        GlTexture nm;
        nm.create();
        nm.allocate(s.w, s.h, rgbaBytes.data());
        GenerateMipmap(TEXTURE_2D);
        nm.setParams(LINEAR_MIPMAP_LINEAR, LINEAR, REPEAT);
        normalMaps_.emplace_back(name, std::move(nm));
    }

    // Overlay texture: re-uploaded every frame with nearest filtering.
    overlayTex_.create();
    overlayTex_.setParams(NEAREST, NEAREST, CLAMP_TO_EDGE);

    // Shadow maps are made on first use (ensureShadowMaps).

    // Internal scene target (its size is a graphics setting; see setRenderScale).
    target_.create(kInternalW, kInternalH);
    return worldProgram_.valid() && quadProgram_.valid() && compositeProgram_.valid() && skyProgram_.valid() && textOk;
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

void SceneRenderer::uploadBatches(BatchList& dst, const std::vector<Batch>& src) {
    for (const auto& b : src) {
        if (b.verts.empty()) continue;
        // Bucket whole triangles by the chunk holding their first vertex.
        std::map<std::pair<int, int>, std::vector<Vertex>> chunks;
        for (size_t i = 0; i + 2 < b.verts.size(); i += 3) {
            const Vertex& v = b.verts[i];
            auto& list = chunks[{static_cast<int>(std::floor(v.px / kChunk)), static_cast<int>(std::floor(v.pz / kChunk))}];
            list.insert(list.end(), b.verts.begin() + static_cast<std::ptrdiff_t>(i),
                        b.verts.begin() + static_cast<std::ptrdiff_t>(i) + 3);
        }
        const GLuint texture = textureFor(b.texture), normal = normalMapFor(b.texture);
        for (const auto& [key, verts] : chunks) {
            GpuBatch g;
            g.vbo.upload(verts.data(), verts.size() * sizeof(Vertex), STATIC_DRAW);
            g.vao.create();
            g.vao.bind();
            setVertexLayout(g.vbo.id());
            BindVertexArray(0);
            g.count = static_cast<int>(verts.size());
            g.texture = texture;
            g.normal = normal;
            g.emissive = b.emissive;
            g.alphaTest = b.alphaTest;
            g.bmin[0] = g.bmin[1] = g.bmin[2] = 1e30f;
            g.bmax[0] = g.bmax[1] = g.bmax[2] = -1e30f;
            for (const Vertex& v : verts) {
                const float p[3] = {v.px, v.py, v.pz};
                for (int k = 0; k < 3; ++k) {
                    g.bmin[k] = std::min(g.bmin[k], p[k]);
                    g.bmax[k] = std::max(g.bmax[k], p[k]);
                }
            }
            dst.push_back(std::move(g));
        }
    }
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

void SceneRenderer::drawBatches(const BatchList& batches, const float* model, const Frustum* cull,
                                float offsetZ) {
    UniformMatrix4fv(uModel_, 1, FALSE_, model);
    for (const auto& b : batches) {
        if (cull && !boxInFrustum(*cull, b.bmin, b.bmax, offsetZ)) continue;
        if (b.texture != drawState_.texture) {
            BindTexture(TEXTURE_2D, b.texture);
            drawState_.texture = b.texture;
        }
        if (bindNormals_ && b.normal != drawState_.normal) {
            ActiveTexture(TEXTURE0 + 6);
            BindTexture(TEXTURE_2D, b.normal);
            ActiveTexture(TEXTURE0);
            drawState_.normal = b.normal;
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
        DrawArrays(TRIANGLES, 0, b.count);
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
    const float cp = std::cos(static_cast<float>(cam.pitch)), sp = std::sin(static_cast<float>(cam.pitch));
    const float cy = std::cos(static_cast<float>(cam.yaw)), sy = std::sin(static_cast<float>(cam.yaw));
    skyProgram_.set("uFwd", cy * cp, sp, sy * cp);
    skyProgram_.set("uRight", -sy, 0.0f, cy);
    skyProgram_.set("uUp", -cy * sp, cp, -sy * sp);
    const float tanHalfY = std::tan(f.fovY * 0.5f);
    skyProgram_.set("uTan", tanHalfY * f.aspect, tanHalfY);
    skyProgram_.set("uTime", static_cast<float>(cam.time));
    skyProgram_.set("uMoonDir", f.moon[0], f.moon[1], f.moon[2]);
    skyProgram_.set("uFogColor", f.fog[0], f.fog[1], f.fog[2]);
    skyProgram_.set("uRes", static_cast<float>(target_.width()), static_cast<float>(target_.height()));
    skyProgram_.set("uGrain", f.grain);
    drawQuad();
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
    {
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
            rad[i] = static_cast<float>(std::max(0.001, l.radius));
        }
        Uniform1i(u.lightCount, n);
        if (n > 0) {
            Uniform3fv(u.lightPos, n, pos);
            Uniform3fv(u.lightColor, n, col);
            Uniform1fv(u.lightRadius, n, rad);
        }
    }
    Uniform1i(u.shadowCount, shadowCount_);
    Uniform1f(u.shadowTexel, 2.0f / static_cast<float>(shadowSize_));
    Uniform1i(u.handShadow, handShadowOn_ ? 1 : 0);
    Uniform3f(u.handPos, handPos_[0], handPos_[1], handPos_[2]);
    for (int i = 0; i < CameraState::kMaxShadowLights; ++i) {
        ActiveTexture(TEXTURE0 + static_cast<GLenum>(1 + i));
        BindTexture(TEXTURE_CUBE_MAP, shadow_[i].texture());
    }
    ActiveTexture(TEXTURE5);
    BindTexture(TEXTURE_CUBE_MAP, handShadow_.texture());
    Uniform2f(u.res, static_cast<float>(target_.width()), static_cast<float>(target_.height()));
    Uniform1f(u.grain, f.grain);
    ActiveTexture(TEXTURE0);
    drawState_ = DrawState{};

    drawScene(cam, f.planes);
}

// Everything that stands in the world this frame, culled against the planes: what the main pass
// draws and what casts shadows.
void SceneRenderer::drawScene(const CameraState& cam, const Frustum& cull) {
    float model[16];
    mat4Identity(model);
    drawBatches(cab_, model, &cull);
    model[14] = static_cast<float>(cam.scrollZ);
    drawBatches(world_, model, &cull, static_cast<float>(cam.scrollZ));
    drawBatches(props_, model, &cull, static_cast<float>(cam.scrollZ));
    for (const auto& d : cam.doors) {
        doorModel(d.hingeX, d.hingeZ, d.angle, model);
        drawBatches(door_, model);
    }
}

// Composite the scene to the window (linear upscale keeps it soft, PS2-like), with the blur and lens.
void SceneRenderer::composite(const CameraState& cam, const Frame& f, int windowW, int windowH) {
    RenderTarget::bindWindow();
    Viewport(0, 0, windowW, windowH);
    ClearColor(0, 0, 0, 1);
    Clear(COLOR_BUFFER_BIT | DEPTH_BUFFER_BIT);
    Disable(DEPTH_TEST);
    Disable(BLEND);

    // Motion blur: the turn since the last frame, as a distance across the picture. A turn of
    // the view moves everything the same way, so a straight smear along it suffices (the sign
    // does not matter: the taps straddle each pixel). Long smears are capped.
    float bx = 0.0f, by = 0.0f;
    if (cam.motionBlur > 0.0 && haveLastView_) {
        double dyaw = cam.yaw - lastYaw_;
        dyaw = std::remainder(dyaw, 6.283185307179586);
        const double dpitch = cam.pitch - lastPitch_;
        const double fovX = 2.0 * std::atan(std::tan(f.fovY * 0.5) * f.aspect);
        // The strength scales the smear; the cap grows with it so the strongest still reads.
        const double cap = 0.08 * std::max(1.0, cam.motionBlur);
        bx = static_cast<float>(std::clamp(dyaw / fovX * cam.motionBlur, -cap, cap));
        by = static_cast<float>(std::clamp(dpitch / f.fovY * cam.motionBlur, -cap, cap));
    }
    lastYaw_ = cam.yaw;
    lastPitch_ = cam.pitch;
    haveLastView_ = true;

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

void SceneRenderer::render(const CameraState& cam, int windowW, int windowH, const Surface* overlay,
                           const TextLayer* text) {
    target_.setSamples(kAntiAliasSamples[std::clamp(cam.antiAlias, 0, kAntiAliasCount - 1)]);
    resizeTarget(windowW, windowH);
    Frame f;
    f.eye[0] = static_cast<float>(cam.x);
    f.eye[1] = static_cast<float>(cam.eyeHeight);
    f.eye[2] = static_cast<float>(cam.z);
    f.dir[0] = static_cast<float>(std::cos(cam.yaw) * std::cos(cam.pitch));
    f.dir[1] = static_cast<float>(std::sin(cam.pitch));
    f.dir[2] = static_cast<float>(std::sin(cam.yaw) * std::cos(cam.pitch));
    {   // the hand light's own aim: the view's, turned by the hand's lag
        const double fy = cam.yaw + cam.handYaw, fp = std::clamp(cam.pitch + cam.handPitch, -1.5, 1.5);
        f.flash[0] = static_cast<float>(std::cos(fy) * std::cos(fp));
        f.flash[1] = static_cast<float>(std::sin(fp));
        f.flash[2] = static_cast<float>(std::sin(fy) * std::cos(fp));
    }
    std::copy(std::begin(kMoonDir), std::end(kMoonDir), f.moon);
    // Fog: a cold blue night outdoors, a near-black warm murk indoors (linear-space colours).
    f.fog[0] = cam.outdoor ? 0.012f : 0.006f;
    f.fog[1] = cam.outdoor ? 0.018f : 0.005f;
    f.fog[2] = cam.outdoor ? 0.034f : 0.005f;
    f.fogDensity = cam.outdoor ? 0.040f : 0.075f;
    f.fovY = kFovY;
    f.aspect = static_cast<float>(target_.width()) / static_cast<float>(target_.height());
    f.grain = cam.grain ? 1.0f : 0.0f;
    const float center[3] = {f.eye[0] + f.dir[0], f.eye[1] + f.dir[1], f.eye[2] + f.dir[2]};
    const float up[3] = {0, 1, 0};
    mat4LookAt(f.eye, center, up, f.view);
    mat4Perspective(f.fovY, f.aspect, 0.05f, 80.0f, f.proj);
    float viewProj[16];
    mat4Multiply(f.proj, f.view, viewProj);
    frustumPlanes(viewProj, f.planes);

    // 1) The lamps' shadow maps, 2) the world into the internal target.
    chooseLamps(cam, f);
    drawShadows(cam);
    target_.bind();
    Viewport(0, 0, target_.width(), target_.height());
    ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    Clear(COLOR_BUFFER_BIT | DEPTH_BUFFER_BIT);
    Disable(BLEND);
    if (cam.outdoor) drawSky(cam, f);
    drawWorld(cam, f);
    target_.resolve();  // a multisampled picture becomes the texture the composite reads

    // 3) The scene to the window, 4) the 2D overlay on top.
    composite(cam, f, windowW, windowH);
    if (overlay && !overlay->empty()) drawOverlay(*overlay, text);
}

DoorInstance doorInstance(const DoorState& d) {
    const double o = d.open * d.open * (3.0 - 2.0 * d.open);  // ease the swing
    const double swing = 1.5707963 * o;
    const double along = 0.5 + 0.2 * d.swing;  // the post face it swings past
    DoorInstance di;
    if (d.passageAlongX) {  // shut, the panel runs south from the north post
        di.hingeX = d.x + along;
        di.hingeZ = d.y + 0.175;
        di.angle = 1.5707963 - d.swing * swing;
    } else {  // shut, the panel runs east from the west post
        di.hingeX = d.x + 0.175;
        di.hingeZ = d.y + along;
        di.angle = d.swing * swing;
    }
    return di;
}

}  // namespace mythos
