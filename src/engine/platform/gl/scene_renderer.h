#ifndef MYTHOS_ENGINE_PLATFORM_GL_SCENE_RENDERER_H_
#define MYTHOS_ENGINE_PLATFORM_GL_SCENE_RENDERER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/mat4.h"
#include "engine/gfx/grade.h"
#include "engine/gfx/mesh.h"
#include "engine/gfx/particles.h"
#include "engine/gfx/render_stats.h"
#include "engine/gfx/surface.h"
#include "engine/gfx/text.h"
#include "engine/gfx/textures.h"
#include "engine/platform/gl/gl_api.h"
#include "engine/platform/gl/gl_objects.h"
#include "engine/platform/gl/text_renderer.h"
#include "engine/world/camera_fx.h"
#include "engine/world/free_cam.h"
#include "engine/world/room_doors.h"

namespace mythos {
// A door panel to draw this frame: its hinge position (world x, z), the height
// of its floor (y) and the panel's rotation about the vertical axis in radians.
struct DoorInstance {
  double hinge_x = 0, hinge_z = 0;
  double y = 0;
  double angle = 0;
};

// Places a door from its game state: the open fraction is eased (smoothstep)
// into a quarter turn, swinging to the side given by `d.swing`, and the hinge
// is offset within the doorway cell according to the passage direction.
DoorInstance MakeDoorInstance(const DoorState& d);

// A point lamp: world position, linear color (may exceed 1) and the radius in
// world units at which its light fades out.
struct PointLight {
  double x = 0, y = 0, z = 0;
  double r = 1, g = 1, b = 1;
  double radius = 4;
  // How much of the light the fog scatters towards the eye, 0..1: the glow
  // round a lamp in the air. 1 for a lamp the eye can see; 0 for a light that
  // only lights (a window's, a pool of headlight), which would otherwise show
  // as a ball of glow floating in the room.
  double halo = 1;
};

// One draw of an uploaded model (see SceneRenderer::UploadModel) at a
// column-major world matrix. `views` selects where it appears: the main view,
// the mirror reflection or both. Instances are never drawn into shadow maps.
struct ModelInstance {
  static constexpr unsigned kMain = 1;
  static constexpr unsigned kMirror = 2;
  // Drawn into the shadow cube maps of the lamps and the hand light (it casts
  // a shadow); add it to `views` of a model that should. An instance with only
  // this flag casts a shadow and is never seen (the player's body).
  static constexpr unsigned kShadow = 4;
  int model = -1;
  float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  unsigned views = kMain | kMirror;
};

// One planar mirror: the glass center (world), its horizontal normal (nx, nz;
// unit length, pointing into the room), half extents of the glass, and how
// opaque and how tinted the reflection is.
struct MirrorState {
  bool on = false;
  double x = 0, y = 0, z = 0;
  double nx = 0, nz = 1;
  double half_w = 0.3, half_h = 0.4;
  double alpha = 0.94;
  // How strongly the hand light glares on the glass (0 off): the spot where
  // its beam strikes it, a streak, and the glint of its reflection.
  double flare = 1.0;

  double tint[3] = {0.80, 0.84, 0.86};
};

// A pane of glass: a vertical rectangle from (x0, z0) to (x1, z1) between the
// heights y0 and y1, tinted and blended over what is behind it (the windows of
// a car). It stands in the world's own space without the scroll, like
// `CameraState::instances`.
struct GlassPane {
  double x0 = 0, z0 = 0, x1 = 0, z1 = 0;
  double y0 = 0, y1 = 0;
  float tint[3] = {0.55f, 0.65f, 0.75f};
  float alpha = 0.18f;
  // Beads of water on the pane, 0 dry .. 1 wet (rain, or dew in the mist).
  float drops = 0.0f;
};

// A box (x/z corners and a y range, world units) lit by lightning when
// `CameraState::lightning_zoned` is set.
struct LightningZone {
  double x0 = 0, z0 = 0, x1 = 0, z1 = 0;
  double y0 = 0, y1 = 0;
};

// Everything the renderer needs for one frame, supplied by the game. Angles are
// radians with pitch positive upward; the view direction is (cos yaw cos pitch,
// sin pitch, sin yaw cos pitch). Lengths are world units (1 cell).
struct CameraState {
  double x = 0, z = 0;
  double eye_height = 1.55;
  double yaw = 0, pitch = 0;
  // Multiplies the vertical field of view (1 = the engine's 62 degrees),
  // clamped to 0.5..1.5; `ApplyCameraFx` sets it for a kick.
  double fov_scale = 1;
  std::vector<DoorInstance> doors;

  // Ambient light color far from lamps and near them (linear RGB).
  float ambient_base[3] = {0.060f, 0.052f, 0.046f};
  float ambient_near[3] = {0.085f, 0.075f, 0.065f};
  // Lamps; only the first `kMaxLights` are used (the cost grows with how many
  // are given), and the nearest lit ones (`kMaxShadowLights`) cast shadows (see
  // `shadow_level`).
  std::vector<PointLight> lights;
  static constexpr int kMaxLights = 32;  // (the shader's arrays are this size)
  static constexpr int kMaxLightningZones = 40;
  static constexpr int kMaxShadowLights = 4;
  // Index into the shadow quality presets in settings.h (0 = no shadows).
  int shadow_level = 3;

  // 0 off, 1 basic, 2 full: how much of the metal/roughness/occlusion model the
  // world shader applies.
  int materials = 2;

  // Strength of the camera-turn smear in the composite pass; 0 is off.
  double motion_blur = 0;

  // Index into `kAntiAliasSamples`; 1 selects FXAA, higher entries MSAA.
  int anti_alias = 1;

  // Lens effects of the composite pass; `bloom` is the glow strength (0 off).
  bool lens_effect = true;
  double bloom = 0;

  // Camcorder tape look, drawn in the composite pass (HUD included).
  bool tape = false;

  // Seconds on the tape counter, shown by the tape look. `torch_tint` (0..1)
  // warms the hand light toward a flame color.
  double tape_time = 0;
  double torch_tint = 0;

  // The hand light (torch or lighter). `hand_yaw` and `hand_pitch` offset its
  // aim from the view direction.
  bool flash_on = false;
  // Where the hand light shines from, in world units (the torch's tip). While
  // `hand_origin_set` the beam's cone and falloff start there and its shadows
  // are cast from there; otherwise the beam starts at the eye.
  bool hand_origin_set = false;
  double hand_x = 0, hand_y = 0, hand_z = 0;
  double hand_yaw = 0, hand_pitch = 0;

  // Hand offset from its rest position, in world units, used to place the hand
  // light's shadow origin.
  double hand_side = 0, hand_down = 0;
  bool lighter = false;
  // Offset added to the z of the world and props, used to scroll a level past
  // the viewer.
  double scroll_z = 0;

  // Outdoor scene: draws the night sky and uses outdoor fog.
  bool outdoor = false;
  bool grain = true;

  // Particle sprites for this frame (`ParticleSystem::Collect`): drawn after
  // the world and before the glass, depth-tested against the scene but not
  // written, translucent ones sorted far to near. At most `kMaxParticles` are
  // drawn. Not shown in the mirror.
  std::vector<ParticleSprite> particles;
  static constexpr int kMaxParticles = 8192;

  // Tone mapping and colour grading (see gfx/grade.h). The default is the
  // engine's original look; values are clamped to sane ranges. A colour table
  // is set with `SceneRenderer::SetColorLut`, mixed in by `grade.lut_amount`.
  ColorGrade grade;
  // Seconds, for animated lamp flicker, sky and grain.
  double time = 0;
  // Lightning flash intensity 0..1, added to ambient light and the sky.
  double lightning = 0;

  // When true the world is lit by lightning only inside `lightning_zones` (up
  // to `kMaxLightningZones`); an empty list lights nothing.
  bool lightning_zoned = false;

  std::vector<LightningZone> lightning_zones;

  // Ground mist: 0 off .. 1 thick. It hugs the ground up to about
  // `mist_height` metres, drifts slowly and swallows the distance (a field at
  // night, a marsh). Works outdoors and in.
  double mist = 0;
  double mist_height = 1.6;

  // Panes of glass drawn over the scene (blended, after the world).
  std::vector<GlassPane> glass;
  // Model instances to draw this frame, and the optional mirror.
  std::vector<ModelInstance> instances;
  MirrorState mirror;
};

// Draws a frame: shadow cube maps, optional mirror view, sky and world into a
// multisampled target, then the composite pass (bloom, motion blur, lens, FXAA)
// to the window, then the 2D overlay and text. Needs a current GL 4.1 context
// and loaded GL functions. Not thread-safe.
class SceneRenderer {
 public:
  // Builds the shaders and fixed GL objects and uploads every texture with its
  // normal and material maps. Returns false if a required program or the text
  // renderer failed to build.
  bool Init(const TextureLibrary& textures);

  // Replaces the world and prop geometry, split into chunks for culling. Every
  // upload invalidates the cached shadow maps.
  void UploadWorld(const SceneGeometry& geometry);

  // Replaces the extra fixed geometry drawn without the scroll offset (world
  // and props of `geometry` together).
  void UploadCab(const SceneGeometry& geometry);

  // Sets the door panel mesh, with its hinge at the origin; instanced per door
  // from `CameraState::doors`.
  void UploadDoorPanel(const std::vector<Batch>& batches);

  // Uploads a model and returns its id for `ModelInstance::model`.
  int UploadModel(const std::vector<Batch>& batches);

  // Sets the colour lookup table of the grade (a strip, see `IdentityLutStrip`
  // in gfx/grade.h), or clears it with an empty or invalid surface. Returns
  // whether a table is now set.
  bool SetColorLut(const Surface& strip);

  // Renders one frame to the default framebuffer of window_w x window_h pixels.
  // The overlay (a BGRA surface covering the window) and text layer are
  // optional and drawn last.
  void Render(const CameraState& cam, int window_w, int window_h,
              const Surface* overlay, const TextLayer* text = nullptr);

  void AddFont(const SdfFont& font) { text_.AddFont(font); }

  // What the last `Render` drew, for the debug overlay.
  [[nodiscard]] const RenderStats& Stats() const { return stats_; }

  // Chooses the internal render height from `kRenderScaleLines`; the width
  // follows the window's aspect ratio.
  void SetRenderScale(int preset);

  // Initial size of the internal render target before the first Render().
  static constexpr int kInternalW = 960;
  static constexpr int kInternalH = 600;

 private:
  // A contiguous vertex range of a batch inside one 8x8 cell column, with its
  // bounding box for frustum culling.
  struct GpuChunk {
    int first = 0, count = 0;
    float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};
  };

  // All geometry of one texture and material state: one VAO and VBO, with its
  // chunks stored contiguously so that neighboring visible chunks merge into a
  // single draw. Texture ids are non-owning (owned by the maps below).
  struct GpuBatch {
    GlVertexArray vao;
    GlBuffer vbo;
    std::vector<GpuChunk> chunks;
    gl::GLuint texture = 0;
    gl::GLuint normal = 0;
    gl::GLuint material = 0;
    bool emissive = false;
    bool alpha_test = false;
    float opacity = 1.0f;
    float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};
  };

  using BatchList = std::vector<GpuBatch>;

  // Per-frame derived values shared by the passes: eye and directions, fog,
  // matrices, culling planes, and the mirror's clip plane (xyz normal, w
  // offset) and which instance views the pass draws.
  struct Frame {
    float eye[3], dir[3], flash[3], moon[3], fog[3];
    float fog_density, fov_y, aspect, grain;
    int materials;
    ColorGrade grade;
    float view[16], proj[16];
    Frustum planes;
    float clip[4] = {0, 0, 0, 1};

    unsigned views = ModelInstance::kMain;
  };

  void LookUpWorldUniforms();

  void BuildFullscreenQuad();

  void UploadTextures(const TextureLibrary& textures);

  // Creates a repeating, trilinear-filtered texture with mipmaps.
  static GlTexture MakeMipmappedTexture(
      int w, int h, const void* rgba, gl::GLenum internal_format = gl::kRgba8);

  // Derives the main view's `Frame` from the camera state.
  Frame MakeFrame(const CameraState& cam) const;

  // Draws sky and world (and the mirror glass) into the main target and
  // resolves multisampling.
  void DrawMainView(const CameraState& cam, const Frame& f, bool mirrored);

  // Computes the screen-space smear from the view change since the last frame,
  // in fractions of the screen, and remembers the current view.
  void MotionBlurSmear(const CameraState& cam, const Frame& f, float& smear_x,
                       float& smear_y);

  // Uploads the chosen lamps (nearest-shadowed first) to the world shader.
  void SetLampUniforms();

  // Binds the lamp and hand-light cube maps to their texture units.
  void BindShadowCubes();

  // Draws each visible door; `model` is scratch space for its matrix.
  void DrawDoors(const CameraState& cam, const Frustum& cull, float* model);

  void DrawInstances(const CameraState& cam, unsigned views);

  // Splits batches into 8-cell chunks, uploads them and resolves their texture
  // maps by name (falling back to "missing").
  void UploadBatches(BatchList& dst, const std::vector<Batch>& src);

  // Binds a batch's textures and flags, skipping what `draw_state_` shows is
  // already bound.
  void BindBatchState(const GpuBatch& batch);

  // Draws batches with the given model matrix, culling against `cull` when
  // given (`offset_z` shifts the boxes to match a scrolled model matrix).
  void DrawBatches(const BatchList& batches, const float* model,
                   const Frustum* cull = nullptr, float offset_z = 0.0f);

  // Draws the fullscreen quad.
  void DrawQuad();

  gl::GLuint TextureFor(const std::string& name) const;

  gl::GLuint NormalMapFor(const std::string& name) const;

  gl::GLuint MaterialMapFor(const std::string& name) const;

  // Uploads the overlay when it changed since the last frame.
  void UploadOverlay(const Surface& overlay);

  void ResizeTarget(int window_w, int window_h);

  // Picks the lamps for this frame, nearest to the eye first among lit ones,
  // and how many of them cast shadows, and sets up the hand light's shadow.
  void ChooseLamps(const CameraState& cam, const Frame& f);

  // Computes the hand light's position and reach and which cube faces its cone
  // can touch.
  void PlaceHandShadow(const CameraState& cam, const Frame& f);

  // Refreshes the shadow cube faces whose lamp or scene changed.
  void DrawShadows(const CameraState& cam);

  // Renders the faces in `face_mask` of one cube from `pos`, skipping each face
  // whose entry in `keys` already equals `base_key`.
  void DrawShadowCube(const CameraState& cam, ShadowCube& cube,
                      const float* pos, float reach, unsigned face_mask,
                      uint64_t* keys, uint64_t base_key);

  // Hash of everything that moves shadow casters: the geometry revision, scroll
  // and door angles.
  uint64_t SceneKey(const CameraState& cam) const;

  // Draws all geometry (cab, world, props, doors, and instances shown in
  // `views`, none when 0) with the current program and state.
  void DrawScene(const CameraState& cam, const Frustum& cull, unsigned views);

  // True if the mirror is on, faces the eye and is within the frustum.
  bool MirrorVisible(const CameraState& cam, const Frame& f) const;

  // Renders the reflected world into the mirror target.
  void DrawMirrorView(const CameraState& cam, const Frame& f);

  // Draws the glass quad sampling the mirror target by screen position.
  void DrawMirrorGlass(const CameraState& cam, const Frame& f);
  // Draws the tinted, blended panes of `cam.glass`.
  void DrawParticles(const CameraState& cam, const Frame& f);

  // One triangle draw, counted in `stats_` (the shadow cubes apart).
  void DrawTris(gl::GLint first, gl::GLsizei count);

  void DrawGlassPanes(const CameraState& cam, const Frame& f);

  // (Re)creates the cube maps when the shadow level changes.
  void EnsureShadowMaps(int level);

  void DrawSky(const CameraState& cam, const Frame& f);

  void DrawWorld(const CameraState& cam, const Frame& f);

  void Composite(const CameraState& cam, const Frame& f, int window_w,
                 int window_h);

  void DrawOverlay(const Surface& overlay, const TextLayer* text);

  // Runs the bright pass and blur into `bloom_b_`; returns false when bloom is
  // off or unavailable.
  bool BuildBloom(const CameraState& cam);

  GlProgram world_program_, quad_program_, sky_program_, shadow_program_,
      composite_program_, mirror_program_, glass_program_, bloom_program_,
      particle_program_;
  RenderTarget bloom_a_, bloom_b_;
  RenderTarget mirror_target_;
  GlVertexArray mirror_vao_;
  GlBuffer mirror_vbo_;
  GlVertexArray glass_vao_, particle_vao_;
  GlBuffer particle_vbo_;
  std::vector<float> particle_verts_;  // reused each frame
  GlBuffer glass_vbo_;
  std::vector<BatchList> models_;
  ShadowCube hand_shadow_;

  // Per-face cache keys: a face is redrawn only when its key changes.
  uint64_t shadow_key_[CameraState::kMaxShadowLights][6] = {};
  uint64_t hand_key_[6] = {};
  // Bit mask of the hand light's cube faces that are drawn (bit i = face i).
  unsigned hand_faces_ = 0x3F;
  // Bumped by every upload so cached shadow faces are redrawn.
  uint64_t scene_revision_ = 1;
  bool hand_shadow_on_ = false;
  float hand_pos_[3] = {0, 0, 0};
  bool hand_origin_on_ = false;  // the beam starts at hand_pos_, not the eye
  float hand_reach_ = 10.0f;
  int shadow_built_ = -1;
  int shadow_size_ = 512;
  int shadow_limit_ = 0;
  bool have_last_view_ = false;
  double last_yaw_ = 0, last_pitch_ = 0;
  DepthBuffer shadow_depth_;

  ShadowCube shadow_[CameraState::kMaxShadowLights];
  std::vector<PointLight> lamps_;
  int shadow_count_ = 0;
  GlVertexArray quad_vao_;
  GlBuffer quad_vbo_;
  RenderTarget target_;
  int render_scale_ = 1;
  GlTexture overlay_tex_;
  RenderStats stats_;
  bool counting_shadows_ = false;
  GlTexture lut_tex_;
  int lut_size_ = 0;
  int overlay_w_ = 0, overlay_h_ = 0;
  std::vector<uint32_t> overlay_pixels_;
  TextRenderer text_;
  std::vector<std::pair<std::string, GlTexture>> textures_;
  std::vector<std::pair<std::string, GlTexture>> normal_maps_;
  std::vector<std::pair<std::string, GlTexture>> material_maps_;
  GlTexture default_material_;
  BatchList world_, props_, cab_, door_, decals_, cab_decals_;

  // The texture and flag state currently bound, to skip redundant GL calls;
  // reset when the program changes.
  struct DrawState {
    gl::GLuint texture = ~0u;
    gl::GLuint normal = ~0u;
    gl::GLuint material = ~0u;
    int emissive = -1, alpha_test = -1;
    float opacity = -1.0f;
  } draw_state_;
  // The solid pass draws batches with opacity 1, the blended pass the others.
  bool blend_pass_ = false;
  gl::GLint u_decal_ = -1;

  gl::GLint u_model_ = -1, u_emissive_ = -1, u_alpha_test_ = -1,
            u_opacity_ = -1;
  // False during shadow passes, which use no normal or material maps.
  bool bind_normals_ = true;

  // Cached uniform locations of the world program.
  struct WorldUniforms {
    gl::GLint view = -1, proj = -1, cam = -1, flash_dir = -1, flash_on = -1,
              lighter = -1, torch_warm = -1, outdoor = -1, moon_dir = -1,
              fog_color = -1, fog_density = -1, time = -1, ambient_base = -1,
              ambient_near = -1, lightning = -1, zone_count = -1, zone_xz = -1,
              zone_y = -1, light_count = -1, light_pos = -1, light_color = -1,
              light_radius = -1, light_halo = -1, shadow_count = -1,
              shadow_texel = -1, hand_shadow = -1, hand_pos = -1,
              hand_origin = -1, hand_faces = -1, res = -1, grain = -1,
              materials = -1, clip = -1, mist = -1, mist_height = -1,
              exposure = -1, tonemap = -1, vignette = -1;
  } world_u_;
};

// Adds a frame's camera shake and field-of-view kick (`CameraFx::Sample`) to
// the camera: yaw and pitch, a sideways and a vertical shift, `fov_scale`.
// Call it after the game has set the camera from the player, every frame; it
// adds to the fields, so do not apply the same offsets twice.
void ApplyCameraFx(const CameraFxOffsets& fx, CameraState& cam);

// Sets the camera's position and look from a free camera (the debug fly
// camera), replacing what the player gave.
void ApplyFreeCam(const FreeCamera& free_cam, CameraState& cam);
}

#endif
