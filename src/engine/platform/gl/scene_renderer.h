#ifndef MYTHOS_ENGINE_PLATFORM_GL_SCENE_RENDERER_H_
#define MYTHOS_ENGINE_PLATFORM_GL_SCENE_RENDERER_H_
// OpenGL 4.1 renderer for a grid world.
//
// The world is drawn into an internal framebuffer with per-pixel lighting
// (torch, moonlight outdoors), distance fog, filmic tone mapping, a vignette
// and gentle film grain, then scaled to the window. Outdoor zones get a
// procedural night sky: stars, a moon and drifting clouds. The 2D overlay (HUD,
// notes, screens) is alpha-blended on top at full window size, and its text is
// drawn over that from distance-field font atlases at the window's own
// resolution.

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/mat4.h"
#include "engine/gfx/mesh.h"
#include "engine/gfx/surface.h"
#include "engine/gfx/text.h"
#include "engine/gfx/textures.h"
#include "engine/platform/gl/gl_api.h"
#include "engine/platform/gl/gl_objects.h"
#include "engine/platform/gl/text_renderer.h"
#include "engine/world/room_doors.h"

namespace mythos {

// One swinging door panel to draw this frame: its hinge on the map plane and
// the absolute angle of its spine (the panel extends kDoorPanelWidth from the
// hinge).
struct DoorInstance {
  double hinge_x = 0, hinge_z = 0;
  double y = 0;  // the floor's height (a house of several floors)
  double angle = 0;
};

// The panel for a door of the level: hinged on the frame face it swings towards
// (the posts fill 0.30..0.70 of the cell along the passage; the wall stops at
// 0.15 across it). Shut, it rests against the posts; a quarter turn lays it
// flat along the wall, clear of posts and wall.
DoorInstance MakeDoorInstance(const DoorState& d);

// A small positional lamp (indoors): colour is the light's final linear RGB,
// already scaled by intensity and any flicker the caller computes this frame.
// The kMaxShadowLights brightest-and-nearest lamps (see CameraState::shadows)
// also cast shadows.
struct PointLight {
  double x = 0, y = 0, z = 0;
  double r = 1, g = 1, b = 1;
  double radius =
      4;  // reach in world units; light fades smoothly to zero there
};

// A model drawn where the caller says, every frame: what moves (the hand's
// torch, the player's body). Upload its batches once with
// SceneRenderer::uploadModel. `matrix` is its model-to-world transform
// (column-major; rotation and uniform scale only, the lighting turns normals
// with it). `views` says where it shows.
struct ModelInstance {
  static constexpr unsigned kMain = 1;    // in the player's own view
  static constexpr unsigned kMirror = 2;  // in the mirror
  int model = -1;                         // the id uploadModel returned
  float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  unsigned views = kMain | kMirror;
};

// A vertical mirror hung on a wall: its glass centre, the horizontal unit
// normal facing into the room, and its size. The scene is drawn again as it is
// seen in it (the lamps and the hand light are real, the picture is reflected),
// then laid over the glass. Only one mirror at a time; the caller sends the one
// the player can see.
struct MirrorState {
  bool on = false;
  double x = 0, y = 0, z = 0;  // centre of the glass
  double nx = 0, nz = 1;       // facing (unit, horizontal)
  double half_w = 0.3, half_h = 0.4;
  double alpha = 0.94;  // how much of the glass shows the reflection (the rest
                        // shows the glass's own texture)
  double tint[3] = {0.80, 0.84, 0.86};  // the glass dims and cools the picture
};

// A box the flash of lightning reaches: a room with a window.
struct LightningZone {
  double x0 = 0, z0 = 0, x1 = 0, z1 = 0;  // on the map plane
  double y0 = 0, y1 = 0;                  // from the floor to the ceiling
};

struct CameraState {
  double x = 0, z = 0;  // eye position on the map plane (x = column, z = row)
  double eye_height = 1.55;
  double yaw = 0, pitch = 0;        // radians
  std::vector<DoorInstance> doors;  // animated door panels
  // Interior light: the base ambient and an extra term near the player
  // (defaults keep the original dark look), plus up to kMaxLights lamps.
  float ambient_base[3] = {0.060f, 0.052f, 0.046f};
  float ambient_near[3] = {0.085f, 0.075f, 0.065f};
  std::vector<PointLight> lights;
  static constexpr int kMaxLights = 8;
  static constexpr int kMaxLightningZones = 40;
  static constexpr int kMaxShadowLights =
      4;                 // lamps that cast shadows: the ones nearest the eye
  int shadow_level = 3;  // shadow maps: 0 off, 1 very low, 2 low, 3 medium, 4
                         // max, 5 ultra max (see shadowSpec)
  int materials = 2;     // 0 off (flat look), 1 specular + metal + occlusion, 2
                         // also reflections (gfx/pbr.h)
  double motion_blur = 0;  // how much turning the view smears the picture: 0
                           // off, 1 normal, 2 double...
  int anti_alias = 1;  // 0 off, 1 FXAA, 2 / 3 / 4 = MSAA 2x / 4x / 8x (what the
                       // GPU lacks is lowered)
  bool lens_effect =
      true;  // colour fringing that grows towards the edges of the picture
  bool flash_on = false;  // torch beam
  double hand_yaw = 0,
         hand_pitch = 0;  // where the hand light points, relative to the view
                          // (radians): it lags and sways
  double hand_side = 0,
         hand_down =
             0;  // and how far the hand has moved from its rest place (metres)
  bool lighter =
      false;  // the hand light is a lighter: wide, short, warm and restless
  double scroll_z = 0;   // the world (not the cab) is drawn this far along z:
                         // the road going by
  bool outdoor = false;  // night sky and moonlight instead of a ceiling
  bool grain = true;     // film grain (a graphics setting)
  double time = 0;       // seconds, for grain and flicker
  double lightning = 0;  // 0..1: a flash of lightning lights the world and the
                         // sky (a thunderstorm; the game decides when)
  bool lightning_zoned = false;  // true: the flash lights the world only inside
                                 // lightning_zones (rooms with a window; none
                                 // at all in a cellar); false: everywhere
  std::vector<LightningZone> lightning_zones;  // at most kMaxLightningZones
  std::vector<ModelInstance>
      instances;  // models drawn this frame (not in the lamps' shadows)
  MirrorState mirror;
};

class SceneRenderer {
 public:
  // Creates shaders, textures and framebuffers. The GL context must be current.
  bool Init(const TextureLibrary& textures);
  void UploadWorld(const SceneGeometry& geometry);
  // A second layer drawn where it stands while the world scrolls past (the cab
  // of a moving car). An empty geometry clears it.
  void UploadCab(const SceneGeometry& geometry);
  void UploadDoorPanel(const std::vector<Batch>& batches);
  // A model for ModelInstance: returns its id (kept until the renderer goes).
  // The batches are in model space.
  int UploadModel(const std::vector<Batch>& batches);
  // overlay may be null. Its size is taken from the Surface and it is stretched
  // over the window. text (may be null) is laid out in overlay pixels and drawn
  // over it at the window's full resolution; its fonts must have been
  // registered with addFont.
  void Render(const CameraState& cam, int window_w, int window_h,
              const Surface* overlay, const TextLayer* text = nullptr);
  // Uploads a font's distance-field atlas. The font must outlive the renderer's
  // use of it.
  void AddFont(const SdfFont& font) { text_.AddFont(font); }
  // Graphics setting: 0 low (300 lines), 1 medium (600), 2 high (900). The
  // internal target takes the window's aspect ratio, so the view widens instead
  // of stretching.
  void SetRenderScale(int preset);

  static constexpr int kInternalW = 960;
  static constexpr int kInternalH = 600;

 private:
  // One texture's geometry: a single VAO + VBO, set up once at upload, holding
  // its triangles sorted by chunk so that each chunk is one contiguous range to
  // cull and draw.
  struct GpuChunk {
    int first = 0, count = 0;  // vertex range in the batch's buffer
    float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};
  };
  struct GpuBatch {
    GlVertexArray vao;
    GlBuffer vbo;
    std::vector<GpuChunk> chunks;
    gl::GLuint texture = 0;  // not owned: see textures_
    gl::GLuint normal = 0;   // its normal + gloss map, likewise
    gl::GLuint material =
        0;  // its metal + occlusion map (the shared default when it has none)
    bool emissive = false;
    bool alpha_test = false;
    float bmin[3] = {0, 0, 0},
          bmax[3] = {0, 0,
                     0};  // bounds of all chunks in model space, for culling
  };
  using BatchList = std::vector<GpuBatch>;

  // Everything the frame's passes share, worked out once in render().
  struct Frame {
    float eye[3], dir[3], flash[3], moon[3], fog[3];
    float fog_density, fov_y, aspect, grain;
    int materials;
    float view[16], proj[16];
    Frustum planes;                // the view frustum, world space
    float clip[4] = {0, 0, 0, 1};  // a plane: what lies on its negative side is
                                   // not drawn (the mirror's back)
    unsigned views = ModelInstance::kMain;  // which instances this pass shows
  };

  // init() steps.
  void LookUpWorldUniforms();
  void BuildFullscreenQuad();
  void UploadTextures(const TextureLibrary& textures);
  static GlTexture MakeMipmappedTexture(
      int w, int h, const void* rgba, gl::GLenum internal_format = gl::kRgba8);

  // render() steps.
  Frame MakeFrame(const CameraState& cam) const;
  void DrawMainView(const CameraState& cam, const Frame& f, bool mirrored);
  void MotionBlurSmear(const CameraState& cam, const Frame& f, float& smear_x,
                       float& smear_y);
  void SetLampUniforms();
  void BindShadowCubes();
  void DrawDoors(const CameraState& cam, const Frustum& cull, float* model);
  void DrawInstances(const CameraState& cam, unsigned views);

  // Splits each batch into spatial chunks (kChunk cells square) so draws can be
  // culled.
  void UploadBatches(BatchList& dst, const std::vector<Batch>& src);
  // Binds a batch's textures, flags and VAO, skipping what drawState_ says is
  // already set.
  void BindBatchState(const GpuBatch& batch);
  // Draws the batches. With a Frame, chunks outside its frustum (shifted by
  // offsetZ) are skipped.
  void DrawBatches(const BatchList& batches, const float* model,
                   const Frustum* cull = nullptr, float offset_z = 0.0f);
  void DrawQuad();
  gl::GLuint TextureFor(const std::string& name) const;
  gl::GLuint NormalMapFor(const std::string& name) const;
  gl::GLuint MaterialMapFor(const std::string& name)
      const;  // the shared default when it has no map of its own
  void UploadOverlay(const Surface& overlay);
  // Reallocates the internal target when the preset or the window's aspect
  // needs another size.
  void ResizeTarget(int window_w, int window_h);

  // Picks the lamps for this frame (shadow casters first) into lamps_ /
  // shadowCount_.
  void ChooseLamps(const CameraState& cam, const Frame& f);
  // Hand light: its position and reach, and the cube faces its beam can reach
  // (handPos_, handReach_, handFaces_).
  void PlaceHandShadow(const CameraState& cam, const Frame& f);
  // Renders the distance cube maps of each shadow-casting lamp and of the hand
  // light.
  void DrawShadows(const CameraState& cam);
  // One lamp's cube map: the scene's distance from `pos` out to `reach`, six
  // faces. Draws the faces in faceMask (bit i = face i), skipping any whose
  // entry in keys[6] already equals baseKey (same lamp, same scene); the
  // entries of the faces drawn are set to baseKey.
  void DrawShadowCube(const CameraState& cam, ShadowCube& cube,
                      const float* pos, float reach, unsigned face_mask,
                      uint64_t* keys, uint64_t base_key);
  // What the scene looks like to a shadow map: the geometry, the scroll and the
  // doors. Equal keys (with the same lamp) mean the cube is still right.
  uint64_t SceneKey(const CameraState& cam) const;
  // views: the ModelInstance::k* bits to show (0 for the shadow passes: nothing
  // held casts a shadow).
  void DrawScene(const CameraState& cam, const Frustum& cull, unsigned views);
  // The mirror: whether it is in view this frame, the scene as the glass shows
  // it, and the glass itself.
  bool MirrorVisible(const CameraState& cam, const Frame& f) const;
  void DrawMirrorView(const CameraState& cam, const Frame& f);
  void DrawMirrorGlass(const CameraState& cam, const Frame& f);
  // (Re)creates the shadow cube maps for a level when it changes.
  void EnsureShadowMaps(int level);
  void DrawSky(const CameraState& cam, const Frame& f);
  void DrawWorld(const CameraState& cam, const Frame& f);
  void Composite(const CameraState& cam, const Frame& f, int window_w,
                 int window_h);
  void DrawOverlay(const Surface& overlay, const TextLayer* text);

  GlProgram world_program_, quad_program_, sky_program_, shadow_program_,
      composite_program_, mirror_program_;
  RenderTarget mirror_target_;  // what the glass shows
  GlVertexArray mirror_vao_;
  GlBuffer mirror_vbo_;
  std::vector<BatchList> models_;  // by ModelInstance::model
  ShadowCube hand_shadow_;         // the torch's or lighter's
  // What each cube was last drawn for (0 = nothing): a static lamp in a still
  // scene is not redrawn. Kept per cube face: the hand light only draws the
  // faces its beam can reach.
  uint64_t shadow_key_[CameraState::kMaxShadowLights][6] = {};
  uint64_t hand_key_[6] = {};
  unsigned hand_faces_ =
      0x3F;  // the faces of the hand light's cube the beam can reach this frame
  uint64_t scene_revision_ = 1;  // bumped by every upload of geometry
  bool hand_shadow_on_ = false;
  float hand_pos_[3] = {0, 0, 0};  // where the hand light is held this frame
  float hand_reach_ = 10.0f;
  int shadow_built_ = -1;  // the level shadow_ was made for
  int shadow_size_ = 512;  // texels per side of a face at that level
  int shadow_limit_ = 0;   // lamps that cast shadows at that quality
  bool have_last_view_ = false;
  double last_yaw_ = 0,
         last_pitch_ = 0;     // the previous frame's view, for motion blur
  DepthBuffer shadow_depth_;  // shared by every cube: they are drawn one face
                              // after another
  ShadowCube shadow_[CameraState::kMaxShadowLights];
  std::vector<PointLight>
      lamps_;  // this frame's lamps, the shadow casters first
  int shadow_count_ = 0;
  GlVertexArray quad_vao_;
  GlBuffer quad_vbo_;
  RenderTarget target_;
  int render_scale_ = 1;
  GlTexture overlay_tex_;
  int overlay_w_ = 0, overlay_h_ = 0;
  std::vector<uint32_t>
      overlay_pixels_;  // what is in overlayTex_, to skip identical frames
  TextRenderer text_;
  std::vector<std::pair<std::string, GlTexture>> textures_;
  std::vector<std::pair<std::string, GlTexture>>
      normal_maps_;  // made from textures_ (gfx/pbr.h)
  std::vector<std::pair<std::string, GlTexture>>
      material_maps_;  // only for textures with a metal or ao companion
  GlTexture
      default_material_;  // metal 0, occlusion 1: what every other texture uses
  BatchList world_, props_, cab_, door_;
  // The state last set by drawBatches, so repeated batches skip redundant GL
  // calls.
  struct DrawState {
    gl::GLuint texture = ~0u;
    gl::GLuint normal = ~0u;
    gl::GLuint material = ~0u;
    int emissive = -1, alpha_test = -1;
  } draw_state_;
  gl::GLint u_model_ = -1, u_emissive_ = -1, u_alpha_test_ = -1;
  bool bind_normals_ =
      true;  // false in the shadow pass, which has no use for them
  // The world program's per-frame uniforms, looked up once in init().
  struct WorldUniforms {
    gl::GLint view = -1, proj = -1, cam = -1, flash_dir = -1, flash_on = -1,
              lighter = -1, outdoor = -1, moon_dir = -1, fog_color = -1,
              fog_density = -1, time = -1, ambient_base = -1, ambient_near = -1,
              lightning = -1, zone_count = -1, zone_xz = -1, zone_y = -1,
              light_count = -1, light_pos = -1, light_color = -1,
              light_radius = -1, shadow_count = -1, shadow_texel = -1,
              hand_shadow = -1, hand_pos = -1, hand_faces = -1, res = -1,
              grain = -1, materials = -1, clip = -1;
  } world_u_;
};

}  // namespace mythos

#endif  // MYTHOS_ENGINE_PLATFORM_GL_SCENE_RENDERER_H_
