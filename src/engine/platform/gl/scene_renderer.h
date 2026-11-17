#pragma once
// OpenGL 4.1 renderer for a grid world.
//
// The world is drawn into an internal framebuffer with per-pixel lighting (torch,
// moonlight outdoors), distance fog, filmic tone mapping, a vignette and gentle film
// grain, then scaled to the window. Outdoor zones get a procedural night sky: stars,
// a moon and drifting clouds. The 2D overlay (HUD, notes, screens) is alpha-blended
// on top at full window size, and its text is drawn over that from distance-field font
// atlases at the window's own resolution.

#include <cstdint>
#include <string>
#include <vector>

#include "engine/platform/gl/gl_api.h"
#include "engine/platform/gl/gl_objects.h"
#include "engine/platform/gl/text_renderer.h"
#include "engine/world/room_doors.h"
#include "engine/core/mat4.h"
#include "engine/gfx/mesh.h"
#include "engine/gfx/surface.h"
#include "engine/gfx/text.h"
#include "engine/gfx/textures.h"

namespace mythos {

// One swinging door panel to draw this frame: its hinge on the map plane and the
// absolute angle of its spine (the panel extends kDoorPanelWidth from the hinge).
struct DoorInstance {
    double hingeX = 0, hingeZ = 0;
    double angle = 0;
};

// The panel for a door of the level: hinged on the frame face it swings towards (the posts
// fill 0.30..0.70 of the cell along the passage; the wall stops at 0.15 across it). Shut, it
// rests against the posts; a quarter turn lays it flat along the wall, clear of posts and wall.
DoorInstance doorInstance(const DoorState& d);

// A small positional lamp (indoors): colour is the light's final linear RGB, already
// scaled by intensity and any flicker the caller computes this frame. The kMaxShadowLights
// brightest-and-nearest lamps (see CameraState::shadows) also cast shadows.
struct PointLight {
    double x = 0, y = 0, z = 0;
    double r = 1, g = 1, b = 1;
    double radius = 4;  // reach in world units; light fades smoothly to zero there
};

struct CameraState {
    double x = 0, z = 0;       // eye position on the map plane (x = column, z = row)
    double eyeHeight = 1.55;
    double yaw = 0, pitch = 0; // radians
    std::vector<DoorInstance> doors;  // animated door panels
    // Interior light: the base ambient and an extra term near the player (defaults keep
    // the original dark look), plus up to kMaxLights lamps.
    float ambientBase[3] = {0.060f, 0.052f, 0.046f};
    float ambientNear[3] = {0.085f, 0.075f, 0.065f};
    std::vector<PointLight> lights;
    static constexpr int kMaxLights = 8;
    static constexpr int kMaxShadowLights = 4;  // lamps that cast shadows: the ones nearest the eye
    int shadowLevel = 3;       // shadow maps: 0 off, 1 very low, 2 low, 3 medium, 4 max, 5 ultra max (see shadowSpec)
    double motionBlur = 0;     // how much turning the view smears the picture: 0 off, 1 normal, 2 double...
    int antiAlias = 1;         // 0 off, 1 FXAA, 2 / 3 / 4 = MSAA 2x / 4x / 8x (what the GPU lacks is lowered)
    bool lensEffect = true;    // colour fringing that grows towards the edges of the picture
    bool flashOn = false;      // torch beam
    bool lighter = false;      // the hand light is a lighter: wide, short, warm and restless
    double scrollZ = 0;        // the world (not the cab) is drawn this far along z: the road going by
    bool outdoor = false;      // night sky and moonlight instead of a ceiling
    bool grain = true;         // film grain (a graphics setting)
    double time = 0;           // seconds, for grain and flicker
};

class SceneRenderer {
public:
    // Creates shaders, textures and framebuffers. The GL context must be current.
    bool init(const TextureLibrary& textures);
    void uploadWorld(const SceneGeometry& geometry);
    // A second layer drawn where it stands while the world scrolls past (the cab of a moving
    // car). An empty geometry clears it.
    void uploadCab(const SceneGeometry& geometry);
    void uploadDoorPanel(const std::vector<Batch>& batches);
    // overlay may be null. Its size is taken from the Surface and it is stretched over the window.
    // text (may be null) is laid out in overlay pixels and drawn over it at the window's
    // full resolution; its fonts must have been registered with addFont.
    void render(const CameraState& cam, int windowW, int windowH, const Surface* overlay,
                const TextLayer* text = nullptr);
    // Uploads a font's distance-field atlas. The font must outlive the renderer's use of it.
    void addFont(const SdfFont& font) { text_.addFont(font); }
    // Graphics setting: 0 low (300 lines), 1 medium (600), 2 high (900). The internal
    // target takes the window's aspect ratio, so the view widens instead of stretching.
    void setRenderScale(int preset);

    static constexpr int kInternalW = 960;
    static constexpr int kInternalH = 600;

private:
    // One textured draw call's worth of geometry: its own VAO + VBO, set up once at upload.
    struct GpuBatch {
        GlVertexArray vao;
        GlBuffer vbo;
        int count = 0;
        gl::GLuint texture = 0;  // not owned: see textures_
        gl::GLuint normal = 0;   // its normal map, likewise
        bool emissive = false;
        bool alphaTest = false;
        float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};  // bounds in model space, for culling
    };
    using BatchList = std::vector<GpuBatch>;

    // Everything the frame's passes share, worked out once in render().
    struct Frame {
        float eye[3], dir[3], moon[3], fog[3];
        float fogDensity, fovY, aspect, grain;
        float view[16], proj[16];
        Frustum planes;  // the view frustum, world space
    };

    // Splits each batch into spatial chunks (kChunk cells square) so draws can be culled.
    void uploadBatches(BatchList& dst, const std::vector<Batch>& src);
    // Draws the batches. With a Frame, chunks outside its frustum (shifted by offsetZ) are skipped.
    void drawBatches(const BatchList& batches, const float* model, const Frustum* cull = nullptr,
                     float offsetZ = 0.0f);
    void drawQuad();
    gl::GLuint textureFor(const std::string& name) const;
    gl::GLuint normalMapFor(const std::string& name) const;
    void uploadOverlay(const Surface& overlay);
    // Reallocates the internal target when the preset or the window's aspect needs another size.
    void resizeTarget(int windowW, int windowH);

    // Picks the lamps for this frame (shadow casters first) into lamps_ / shadowCount_.
    void chooseLamps(const CameraState& cam, const Frame& f);
    // Renders the distance cube maps of each shadow-casting lamp and of the hand light.
    void drawShadows(const CameraState& cam);
    // One lamp's cube map: the scene's distance from `pos` out to `reach`, six faces.
    void drawShadowCube(const CameraState& cam, ShadowCube& cube, const float* pos, float reach);
    // What the scene looks like to a shadow map: the geometry, the scroll and the doors. Equal
    // keys (with the same lamp) mean the cube is still right.
    uint64_t sceneKey(const CameraState& cam) const;
    void drawScene(const CameraState& cam, const Frustum& cull);
    // (Re)creates the shadow cube maps for a level when it changes.
    void ensureShadowMaps(int level);
    void drawSky(const CameraState& cam, const Frame& f);
    void drawWorld(const CameraState& cam, const Frame& f);
    void composite(const CameraState& cam, const Frame& f, int windowW, int windowH);
    void drawOverlay(const Surface& overlay, const TextLayer* text);

    GlProgram worldProgram_, quadProgram_, skyProgram_, shadowProgram_, compositeProgram_;
    ShadowCube handShadow_;    // the torch's or lighter's
    // What each cube was last drawn for (0 = nothing): a static lamp in a still scene is not redrawn.
    uint64_t shadowKey_[CameraState::kMaxShadowLights] = {};
    uint64_t handKey_ = 0;
    uint64_t sceneRevision_ = 1;  // bumped by every upload of geometry
    bool handShadowOn_ = false;
    float handPos_[3] = {0, 0, 0};  // where the hand light is held this frame
    float handReach_ = 10.0f;
    int shadowBuilt_ = -1;     // the level shadow_ was made for
    int shadowSize_ = 512;     // texels per side of a face at that level
    int shadowLimit_ = 0;      // lamps that cast shadows at that quality
    bool haveLastView_ = false;
    double lastYaw_ = 0, lastPitch_ = 0;  // the previous frame's view, for motion blur
    DepthBuffer shadowDepth_;  // shared by every cube: they are drawn one face after another
    ShadowCube shadow_[CameraState::kMaxShadowLights];
    std::vector<PointLight> lamps_;  // this frame's lamps, the shadow casters first
    int shadowCount_ = 0;
    GlVertexArray quadVao_;
    GlBuffer quadVbo_;
    RenderTarget target_;
    int renderScale_ = 1;
    GlTexture overlayTex_;
    int overlayW_ = 0, overlayH_ = 0;
    std::vector<uint32_t> overlayPixels_;  // what is in overlayTex_, to skip identical frames
    TextRenderer text_;
    std::vector<std::pair<std::string, GlTexture>> textures_;
    std::vector<std::pair<std::string, GlTexture>> normalMaps_;  // made from textures_ (gfx/normalmap.h)
    BatchList world_, props_, cab_, door_;
    // The state last set by drawBatches, so repeated batches skip redundant GL calls.
    struct DrawState {
        gl::GLuint texture = ~0u;
        gl::GLuint normal = ~0u;
        int emissive = -1, alphaTest = -1;
    } drawState_;
    gl::GLint uModel_ = -1, uEmissive_ = -1, uAlphaTest_ = -1;
    bool bindNormals_ = true;  // false in the shadow pass, which has no use for them
    // The world program's per-frame uniforms, looked up once in init().
    struct WorldUniforms {
        gl::GLint view = -1, proj = -1, cam = -1, flashDir = -1, flashOn = -1, lighter = -1, outdoor = -1, moonDir = -1,
                  fogColor = -1, fogDensity = -1, time = -1, ambientBase = -1, ambientNear = -1, lightCount = -1,
                  lightPos = -1, lightColor = -1, lightRadius = -1, shadowCount = -1, shadowTexel = -1,
                  handShadow = -1, handPos = -1, res = -1, grain = -1;
    } world_u_;
};

}  // namespace mythos
