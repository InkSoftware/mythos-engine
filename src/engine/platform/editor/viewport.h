#ifndef MYTHOS_ENGINE_PLATFORM_EDITOR_VIEWPORT_H_
#define MYTHOS_ENGINE_PLATFORM_EDITOR_VIEWPORT_H_
// The editor's 3D view: an orbit / fly camera, the level's geometry drawn in
// an offscreen target (a texture the UI shows), and immediate-mode lines and
// translucent boxes for the grid, selection and gizmos. OpenGL 4.1 core through
// the engine's loader. The vector maths here is also what picking uses.

#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "engine/gfx/mesh.h"
#include "engine/gfx/textures.h"
#include "engine/platform/gl/gl_objects.h"

namespace mythos::editor {

struct Vec3 {
  float x = 0, y = 0, z = 0;
};
inline Vec3 operator+(Vec3 a, Vec3 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator-(Vec3 a, Vec3 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
inline Vec3 Normalize(Vec3 a) {
  const float l = Length(a);
  return l > 1e-9f ? a * (1.0f / l) : Vec3{0, 1, 0};
}

struct Mat4 {
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                 0, 0, 1, 0, 0, 0, 0, 1};  // column-major
};
Mat4 Multiply(const Mat4& a, const Mat4& b);

struct Ray {
  Vec3 o, d;  // d is a unit vector
  Vec3 At(float t) const { return o + d * t; }
};

// Ray against a plane y = height: the distance along the ray, or -1.
float RayHitsHorizontalPlane(const Ray& r, float height);
// Ray against an oriented box (yaw about the vertical axis, in radians): the
// distance to the entry, or -1. centre is the middle of the box.
float RayHitsBox(const Ray& r, Vec3 centre, Vec3 half, float yaw);
float RayHitsSphere(const Ray& r, Vec3 centre, float radius);
// The point of the line (a, direction da) nearest the ray, as a distance along
// the line; used to drag along an axis. False when they are parallel.
bool NearestOnLine(const Ray& r, Vec3 a, Vec3 da, float* t);

// An orbit camera around a target; yaw is a heading in the game's degrees
// convention (0 east, 90 south), pitch looks down when positive.
class OrbitCamera {
 public:
  Vec3 target{8, 0, 6};
  float heading = 0.0f;  // radians the eye looks along (x = cos, z = sin)
  float pitch = 0.75f;   // radians below the horizon
  float distance = 22.0f;
  float fov_deg = 55.0f;

  Vec3 Forward() const;
  Vec3 Eye() const { return target - Forward() * distance; }
  Vec3 Right() const { return Normalize(Cross(Forward(), {0, 1, 0})); }
  Mat4 View() const;
  Mat4 Projection(float aspect) const;
  // nx, ny in -1..1 (right and up).
  Ray PickRay(float nx, float ny, float aspect) const;
  void Orbit(float d_heading, float d_pitch);
  // Pans by screen pixels in a view of height_px pixels.
  void Pan(float dx_px, float dy_px, float height_px);
  void Zoom(float wheel);  // wheel > 0 moves in
  // Moves eye and target together: forward, right, up in metres.
  void Fly(float forward, float right, float up);
  // Turns in place (the eye stays), as with the right mouse button held.
  void Look(float d_heading, float d_pitch);
  void FocusOn(Vec3 point, float radius);
  void TopDown() { pitch = 1.5533f; }
  // Screen position (pixels, origin top-left) of a world point; false when
  // behind.
  bool Project(Vec3 p, float w, float h, float* sx, float* sy) const;
};

struct Rgba {
  float r = 1, g = 1, b = 1, a = 1;
};

struct LayerStyle {
  bool visible = true;
  float tint = 1.0f;    // brightness
  float alpha = 1.0f;   // 1 solid; a ghost floor is drawn faint
  float clip_y = 1e9f;  // nothing above this height is drawn (the cutaway)
};

enum class ShadeMode { kTextured, kFlat, kWireframe };

class Viewport {
 public:
  bool Init(const mythos::TextureLibrary* textures);
  // Replaces the geometry of a layer (an empty list clears it).
  void SetLayer(int slot, const std::vector<mythos::Batch>& batches);
  void SetStyle(int slot, const LayerStyle& style);
  // The GL texture of a named texture (made on first use), for the palette.
  unsigned TextureId(const std::string& name);
  static constexpr int kSlots = 8;
  static constexpr int kEntitySlot =
      7;  // the models of props, lit like the rest

  // Immediate overlay for one frame.
  void ClearOverlay();
  void Line(Vec3 a, Vec3 b, Rgba c, bool on_top = false);
  void WireBox(Vec3 centre, Vec3 half, float yaw, Rgba c, bool on_top = false);
  void Circle(Vec3 centre, float radius, Rgba c, bool on_top = false,
              int n = 48);
  void Arrow(Vec3 from, Vec3 to, Rgba c, float head);
  void SolidBox(Vec3 centre, Vec3 half, float yaw, Rgba c);  // translucent
  void Quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Rgba col);       // translucent

  // Draws the scene into the target (resizing it) and resolves it. Returns the
  // texture the UI shows (bottom-up).
  unsigned Render(const OrbitCamera& cam, int w, int h, ShadeMode mode,
                  Rgba background);

 private:
  struct GpuBatch {
    mythos::GlVertexArray vao;
    mythos::GlBuffer vbo;
    int count = 0;
    std::string texture;
    bool emissive = false, alpha_test = false;
  };
  struct Layer {
    std::vector<GpuBatch> batches;
    LayerStyle style;
  };
  struct ColorVertex {
    float x, y, z, r, g, b, a;
  };
  unsigned TextureFor(const std::string& name);
  void DrawLayer(const Layer& layer, const Mat4& mvp, ShadeMode mode);
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

}  // namespace mythos::editor

#endif  // MYTHOS_ENGINE_PLATFORM_EDITOR_VIEWPORT_H_
