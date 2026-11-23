#ifndef MYTHOS_ENGINE_PLATFORM_EDITOR_VIEWPORT_H_
#define MYTHOS_ENGINE_PLATFORM_EDITOR_VIEWPORT_H_

// Small math types and the editor's own GL renderer: an orbit camera, picking
// rays, and a viewport that draws level geometry plus a line/triangle overlay
// into an offscreen target that ImGui shows as an image.
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "engine/gfx/mesh.h"
#include "engine/gfx/textures.h"
#include "engine/platform/gl/gl_objects.h"

namespace mythos::editor {
// A 3D vector in world units (x = column, z = row, y up).
struct Vec3 {
  float x = 0, y = 0, z = 0;
};

inline Vec3 operator+(const Vec3 a, const Vec3 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 operator-(const Vec3 a, const Vec3 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 operator*(const Vec3 a, const float s) {
  return {a.x * s, a.y * s, a.z * s};
}

inline float Dot(const Vec3 a, const Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 Cross(const Vec3 a, const Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float Length(const Vec3 a) { return std::sqrt(Dot(a, a)); }

inline Vec3 Normalize(const Vec3 a) {
  const float l = Length(a);
  return l > 1e-9f ? a * (1.0f / l) : Vec3{0, 1, 0};
}

// A 4x4 matrix in column-major order (OpenGL layout): m[col * 4 + row].
struct Mat4 {
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

// Returns a * b, so that b is applied first.
Mat4 Multiply(const Mat4& a, const Mat4& b);

// A ray with origin o and unit direction d.
struct Ray {
  Vec3 o, d;
  [[nodiscard]] Vec3 At(const float t) const { return o + d * t; }
};

// Returns the distance t >= 0 along the ray to the plane y = height, or -1 when
// the ray is parallel to it or the plane is behind the origin.
float RayHitsHorizontalPlane(const Ray& r, float height);

// Returns the entry distance of the ray into a box, or -1 on a miss. The box
// is centerd at `centre`, has half extents `half` in its own frame, and is
// turned by `yaw` radians about the y axis. An origin inside gives 0.
float RayHitsBox(const Ray& r, Vec3 centre, Vec3 half, float yaw);

// Returns the entry distance into a sphere (0 when the origin is inside), or
// -1 on a miss.
float RayHitsSphere(const Ray& r, Vec3 centre, float radius);

// Finds the parameter *t along the line a + t * da that is closest to the
// ray. Returns false when the two are (nearly) parallel.
bool NearestOnLine(const Ray& r, Vec3 a, Vec3 da, float* t);

// A camera orbiting `target` at `distance`, with heading and pitch in radians.
// Heading 0 looks along +x; positive pitch looks down. The pitch is clamped
// short of vertical so the view basis stays defined.
class OrbitCamera {
 public:
  Vec3 target{8, 0, 6};
  float heading = 0.0f;
  float pitch = 0.75f;
  float distance = 22.0f;
  float fov_deg = 55.0f;

  // Unit view direction.
  [[nodiscard]] Vec3 Forward() const;

  [[nodiscard]] Vec3 Eye() const { return target - Forward() * distance; }

  [[nodiscard]] Vec3 Right() const {
    return Normalize(Cross(Forward(), {0, 1, 0}));
  }

  // World to eye space.
  [[nodiscard]] Mat4 View() const;

  // Perspective projection (vertical field of view `fov_deg`, near 0.05, far
  // 600) for the given width / height ratio.
  [[nodiscard]] Mat4 Projection(float aspect) const;

  // The ray through a point of the view in normalized device coordinates
  // (nx, ny in -1..1, y up).
  [[nodiscard]] Ray PickRay(float nx, float ny, float aspect) const;

  // Turns the camera about the target.
  void Orbit(float d_heading, float d_pitch);

  // Slides the target in the view plane by a mouse movement in pixels;
  // `height_px` is the viewport height, which sets the world size of a pixel.
  void Pan(float dx_px, float dy_px, float height_px);

  // Scales the distance by 0.88 per wheel notch (clamped to 0.5..300).
  void Zoom(float wheel);

  // Moves the camera (and its target) along the view direction, to the right
  // and straight up, in world units.
  void Fly(float forward, float right, float up);

  // Turns the camera about its own eye, keeping the eye fixed (free look).
  void Look(float d_heading, float d_pitch);

  // Centers on a point and backs off to a distance that fits `radius`.
  void FocusOn(Vec3 point, float radius);

  // Looks (almost) straight down.
  void TopDown() { pitch = 1.5533f; }

  // Projects a world point to pixel coordinates of a w x h view (origin at the
  // top left). Returns false when the point is behind the camera.
  bool Project(Vec3 p, float w, float h, float* sx, float* sy) const;
};

// A color with straight (non-premultiplied) alpha, each channel 0..1.
struct Rgba {
  float r = 1, g = 1, b = 1, a = 1;
};

// How one geometry layer is drawn: `tint` scales the color, `alpha` < 1 makes
// it a translucent ghost, and fragments above `clip_y` are dropped (the
// cutaway).
struct LayerStyle {
  bool visible = true;
  float tint = 1.0f;
  float alpha = 1.0f;
  float clip_y = 1e9f;
};

// How world layers are shaded.
enum class ShadeMode { kTextured, kFlat, kWireframe };

// The editor's own small renderer. World geometry is held in kSlots layers
// (kEntitySlot is for model props); an overlay of lines and triangles is
// rebuilt every frame. Needs a current GL context; not thread-safe.
class Viewport {
 public:
  // Compiles the shaders and creates the target. Returns false on a GL error.
  // `textures` must outlive the viewport.
  bool Init(const mythos::TextureLibrary* textures);

  // Replaces the geometry of a layer (uploads one GPU buffer per batch).
  void SetLayer(int slot, const std::vector<mythos::Batch>& batches);

  // Replaces the decals belonging to a layer.
  void SetDecals(int slot, const std::vector<mythos::Batch>& batches);

  // Sets how a layer is drawn.
  void SetStyle(int slot, const LayerStyle& style);

  // The GL texture of a named texture, uploaded on first use, for use as an
  // ImGui image. Owned by the viewport.
  unsigned TextureId(const std::string& name);

  static constexpr int kSlots = 8;
  static constexpr int kEntitySlot = 7;

  // Drops the overlay shapes; call at the start of each frame.
  void ClearOverlay();

  // Overlay line. `on_top` draws it without depth testing.
  void Line(Vec3 a, Vec3 b, Rgba c, bool on_top = false);

  // Overlay wireframe box, turned by `yaw` radians about y.
  void WireBox(Vec3 centre, Vec3 half, float yaw, Rgba c, bool on_top = false);

  // Overlay circle of `n` segments in the horizontal plane.
  void Circle(Vec3 centre, float radius, Rgba c, bool on_top = false,
              int n = 48);

  // Overlay arrow from `from` to `to` with a four-line head `head` long.
  void Arrow(Vec3 from, Vec3 to, Rgba c, float head);

  // Translucent overlay box (blended, no depth write).
  void SolidBox(Vec3 centre, Vec3 half, float yaw, Rgba c);

  // Translucent overlay quad; the corners a, b, c, d go round in order.
  void Quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Rgba col);

  // Draws the layers and overlay at w x h pixels (at least 16 x 16) and returns
  // the GL texture id of the resolved color image. Restores the window as the
  // render target.
  unsigned Render(const OrbitCamera& cam, int w, int h, ShadeMode mode,
                  Rgba background);

 private:
  // One uploaded batch: a vertex array of `mythos::Vertex` for one texture.
  struct GpuBatch {
    mythos::GlVertexArray vao;
    mythos::GlBuffer vbo;
    int count = 0;
    std::string texture;
    bool emissive = false, alpha_test = false;
  };

  struct Layer {
    std::vector<GpuBatch> batches;
    std::vector<GpuBatch> decals;
    LayerStyle style;
  };

  struct ColorVertex {
    float x, y, z, r, g, b, a;
  };

  // Uploads (or finds) a texture; same as TextureId.
  unsigned TextureFor(const std::string& name);

  void UploadBatches(std::vector<GpuBatch>& into,
                     const std::vector<mythos::Batch>& batches);
  void DrawLayer(const Layer& layer, const Mat4& mvp, ShadeMode mode,
                 bool decals = false);

  void DrawColor(const std::vector<ColorVertex>& v, unsigned mode,
                 const Mat4& mvp, bool depth_test);

  const mythos::TextureLibrary* textures_ = nullptr;
  mythos::GlProgram world_, color_;
  mythos::RenderTarget target_;
  mythos::GlVertexArray color_vao_;
  mythos::GlBuffer color_vbo_;
  std::map<std::string, mythos::GlTexture> gl_textures_;
  Layer layers_[kSlots];
  std::vector<ColorVertex> lines_, lines_top_, tris_;
};
}

#endif
