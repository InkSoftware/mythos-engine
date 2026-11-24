#include "engine/platform/editor/viewport.h"

// Viewport renderer and camera / picking math for the level editor.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

#include "engine/platform/gl/gl_api.h"

namespace mythos::editor {
namespace {
constexpr float kPi = std::numbers::pi_v<float>;
// Enums that gl_api does not declare (the engine's GL subset has no line or
// polygon-mode entry points).
constexpr gl::GLenum kLines = 0x0001;
constexpr gl::GLenum kFrontAndBack = 0x0408;
constexpr gl::GLenum kLineMode = 0x1B01;
constexpr gl::GLenum kFillMode = 0x1B02;

// Entry points the engine's loader does not provide, fetched in Init. Either
// may stay null, which just disables the wireframe mode or the depth-write
// toggle.
void(MYTHOS_GL_CALL* g_depth_mask)(gl::GLboolean) = nullptr;
void(MYTHOS_GL_CALL* g_polygon_mode)(gl::GLenum, gl::GLenum) = nullptr;

// The world shader: fixed sun and fill lights plus a hemisphere term, so the
// editor needs no lamp or shadow setup. u_clip_y implements the cutaway.
const char* kWorldVs = R"(#version 410 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
uniform mat4 u_mvp;
out vec3 v_normal;
out vec3 v_pos;
out vec2 v_uv;
void main() {
  v_normal = a_normal;
  v_pos = a_pos;
  v_uv = a_uv;
  gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

const char* kWorldFs = R"(#version 410 core
in vec3 v_normal;
in vec3 v_pos;
in vec2 v_uv;
uniform sampler2D u_tex;
uniform vec4 u_tint;
uniform float u_clip_y;
uniform int u_flat;
uniform int u_emissive;
uniform int u_alpha_test;
uniform int u_decal;
out vec4 o_color;
void main() {
  if (v_pos.y > u_clip_y) discard;
  vec4 texel = texture(u_tex, v_uv);
  vec4 t = u_flat == 1 ? vec4(0.72, 0.72, 0.74, 1.0) : texel;
  if (u_alpha_test == 1 && t.a < 0.5) discard;
  vec3 n = normalize(v_normal);
  float sun = max(dot(n, normalize(vec3(0.35, 0.85, 0.4))), 0.0);
  float fill = max(dot(n, normalize(vec3(-0.6, 0.3, -0.5))), 0.0);
  float hemi = 0.5 + 0.5 * n.y;
  float light = u_emissive == 1 ? 1.0 : 0.30 + 0.20 * hemi + 0.55 * sun + 0.18 * fill;
  o_color = vec4(t.rgb * light * u_tint.rgb, u_tint.a * (u_decal == 1 ? texel.a : 1.0));
}
)";

// The overlay shader: vertex colors, no lighting.
const char* kColorVs = R"(#version 410 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_color;
uniform mat4 u_mvp;
out vec4 v_color;
void main() {
  v_color = a_color;
  gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

const char* kColorFs = R"(#version 410 core
in vec4 v_color;
out vec4 o_color;
void main() { o_color = v_color; }
)";
}

// Column-major product, a * b.
Mat4 Multiply(const Mat4& a, const Mat4& b) {
  Mat4 r;
  for (int c = 0; c < 4; ++c)
    for (int row = 0; row < 4; ++row) {
      float s = 0;
      for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
      r.m[c * 4 + row] = s;
    }
  return r;
}

float RayHitsHorizontalPlane(const Ray& r, const float height) {
  if (std::abs(r.d.y) < 1e-6f) return -1;
  const float t = (height - r.o.y) / r.d.y;
  return t >= 0 ? t : -1;
}

// Slab test in the box's own frame: the ray is turned by -yaw about the box
// center so the box becomes axis aligned.
float RayHitsBox(const Ray& r, const Vec3 centre, const Vec3 half,
                 const float yaw) {
  const float c = std::cos(-yaw), s = std::sin(-yaw);
  auto turn = [&](const Vec3 v) {
    return Vec3{v.x * c - v.z * s, v.y, v.x * s + v.z * c};
  };
  const Vec3 o = turn(r.o - centre), d = turn(r.d);
  float t0 = 0, t1 = 1e30f;
  const float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z},
              hh[3] = {half.x, half.y, half.z};
  for (int i = 0; i < 3; ++i) {
    if (std::abs(dd[i]) < 1e-9f) {
      if (oo[i] < -hh[i] || oo[i] > hh[i]) return -1;
      continue;
    }
    float a = (-hh[i] - oo[i]) / dd[i], b = (hh[i] - oo[i]) / dd[i];
    if (a > b) std::swap(a, b);
    t0 = std::max(t0, a);
    t1 = std::min(t1, b);
    if (t0 > t1) return -1;
  }
  return t0;
}

// Falls back to the far root (returning 0) when the origin is inside.
float RayHitsSphere(const Ray& r, const Vec3 centre, const float radius) {
  const Vec3 oc = r.o - centre;
  const float b = Dot(oc, r.d), c = Dot(oc, oc) - radius * radius;
  const float disc = b * b - c;
  if (disc < 0) return -1;
  const float t = -b - std::sqrt(disc);
  return t >= 0 ? t : (-b + std::sqrt(disc) >= 0 ? 0.0f : -1.0f);
}

// Closest approach of two lines, solved for the parameter on the first.
bool NearestOnLine(const Ray& r, const Vec3 a, const Vec3 da, float* const t) {
  const Vec3 w = a - r.o;
  const float b = Dot(da, r.d), d = Dot(da, w), e = Dot(r.d, w);
  const float den = Dot(da, da) - b * b;
  if (std::abs(den) < 1e-6f) return false;
  *t = (b * e - d) / den;
  return true;
}

// Heading turns about y (0 looks along +x); positive pitch tilts downward.
Vec3 OrbitCamera::Forward() const {
  const float cp = std::cos(pitch);
  return {cp * std::cos(heading), -std::sin(pitch), cp * std::sin(heading)};
}

// Right-handed look-at. For a vertical view the side vector would be
// degenerate, so +z stands in for the up hint. PickRay and Project build the
// same basis.
Mat4 OrbitCamera::View() const {
  const Vec3 eye = Eye(), f = Forward();
  const Vec3 up0{0, 1, 0};
  const Vec3 s =
      Normalize(Cross(f, std::abs(f.y) > 0.9999f ? Vec3{0, 0, 1} : up0));
  const Vec3 u = Cross(s, f);
  Mat4 m;
  m.m[0] = s.x;
  m.m[4] = s.y;
  m.m[8] = s.z;
  m.m[12] = -Dot(s, eye);
  m.m[1] = u.x;
  m.m[5] = u.y;
  m.m[9] = u.z;
  m.m[13] = -Dot(u, eye);
  m.m[2] = -f.x;
  m.m[6] = -f.y;
  m.m[10] = -f.z;
  m.m[14] = Dot(f, eye);
  m.m[3] = 0;
  m.m[7] = 0;
  m.m[11] = 0;
  m.m[15] = 1;
  return m;
}

// Standard OpenGL perspective; fov_deg is vertical.
Mat4 OrbitCamera::Projection(const float aspect) const {
  const float n = 0.05f, f = 600.0f;
  const float t = 1.0f / std::tan(fov_deg * kPi / 360.0f);
  Mat4 m;
  std::memset(m.m, 0, sizeof m.m);
  m.m[0] = t / aspect;
  m.m[5] = t;
  m.m[10] = (f + n) / (n - f);
  m.m[11] = -1;
  m.m[14] = 2 * f * n / (n - f);
  return m;
}

Ray OrbitCamera::PickRay(const float nx, const float ny,
                         const float aspect) const {
  const Vec3 f = Forward();
  const Vec3 s = Normalize(
      Cross(f, std::abs(f.y) > 0.9999f ? Vec3{0, 0, 1} : Vec3{0, 1, 0}));
  const Vec3 u = Cross(s, f);
  const float th = std::tan(fov_deg * kPi / 360.0f);
  const Vec3 d = Normalize(f + s * (nx * th * aspect) + u * (ny * th));
  return {Eye(), d};
}

// The pitch limits keep the camera off the poles (1.5533 rad is about 89
// degrees, which is also what TopDown uses).
void OrbitCamera::Orbit(const float dh, const float dp) {
  heading += dh;
  pitch = std::clamp(pitch + dp, -1.5f, 1.5533f);
}

void OrbitCamera::Pan(const float dx, const float dy, const float height_px) {
  const float world_per_px = 2.0f * distance *
                             std::tan(fov_deg * kPi / 360.0f) /
                             std::max(1.0f, height_px);
  const Vec3 f = Forward();
  const Vec3 s = Normalize(
      Cross(f, std::abs(f.y) > 0.9999f ? Vec3{0, 0, 1} : Vec3{0, 1, 0}));
  const Vec3 u = Cross(s, f);
  target = target - s * (dx * world_per_px) + u * (dy * world_per_px);
}

// Exponential, so each notch is the same relative step at any distance.
void OrbitCamera::Zoom(const float wheel) {
  distance = std::clamp(distance * std::pow(0.88f, wheel), 0.5f, 300.0f);
}

void OrbitCamera::Fly(const float forward, const float right, const float up) {
  const Vec3 f = Forward();
  target = target + f * forward + Right() * right + Vec3{0, up, 0};
}

// Re-aims about the eye: the target is moved so the eye stays put.
void OrbitCamera::Look(const float dh, const float dp) {
  const Vec3 eye = Eye();
  heading += dh;
  pitch = std::clamp(pitch + dp, -1.5f, 1.5533f);
  target = eye + Forward() * distance;
}

void OrbitCamera::FocusOn(const Vec3 point, const float radius) {
  target = point;
  distance = std::clamp(radius * 3.0f, 2.0f, 80.0f);
}

bool OrbitCamera::Project(const Vec3 p, const float w, const float h,
                          float* const sx, float* const sy) const {
  const Vec3 rel = p - Eye();
  const Vec3 f = Forward();
  const float depth = Dot(rel, f);
  if (depth < 0.05f) return false;
  const Vec3 s = Normalize(
      Cross(f, std::abs(f.y) > 0.9999f ? Vec3{0, 0, 1} : Vec3{0, 1, 0}));
  const Vec3 u = Cross(s, f);
  const float th = std::tan(fov_deg * kPi / 360.0f);
  const float aspect = w / h;
  const float nx = Dot(rel, s) / (depth * th * aspect);
  const float ny = Dot(rel, u) / (depth * th);
  *sx = (nx * 0.5f + 0.5f) * w;
  *sy = (0.5f - ny * 0.5f) * h;
  return true;
}

// Attribute locations match the vertex layout of mythos::Vertex (position,
// normal, uv) and of ColorVertex (position, color).
bool Viewport::Init(const mythos::TextureLibrary* const textures) {
  textures_ = textures;
  g_depth_mask = reinterpret_cast<decltype(g_depth_mask)>(
      SDL_GL_GetProcAddress("glDepthMask"));
  g_polygon_mode = reinterpret_cast<decltype(g_polygon_mode)>(
      SDL_GL_GetProcAddress("glPolygonMode"));
  if (!world_.Build(kWorldVs, kWorldFs,
                    {{0, "a_pos"}, {1, "a_normal"}, {2, "a_uv"}}) ||
      !color_.Build(kColorVs, kColorFs, {{0, "a_pos"}, {1, "a_color"}}))
    return false;
  color_vao_.Create();
  color_vbo_.Create();
  target_.Create(16, 16);
  target_.SetSamples(4);
  return true;
}

unsigned Viewport::TextureId(const std::string& name) {
  return TextureFor(name);
}

// Uploads flipped rows (the surfaces are stored top-down; GL wants bottom-up)
// with mipmaps, and keeps the texture for the life of the viewport.
unsigned Viewport::TextureFor(const std::string& name) {
  auto it = gl_textures_.find(name);
  if (it != gl_textures_.end()) return it->second.Id();
  const mythos::Surface& s = textures_->Get(name);
  std::vector<uint8_t> rgba;
  mythos::SurfaceToRgbaFlipped(s, rgba);
  mythos::GlTexture tex;
  tex.Create();
  tex.Allocate(s.w, s.h, rgba.data());
  gl::GenerateMipmap(gl::kTexture2D);
  tex.SetParams(gl::kLinearMipmapLinear, gl::kLinear, gl::kRepeat);
  const unsigned id = tex.Id();
  gl_textures_.emplace(name, std::move(tex));
  return id;
}

// mythos::Vertex is laid out as position (3 floats), normal (3), uv (2),
// which the attribute pointers below mirror.
void Viewport::SetLayer(const int slot,
                        const std::vector<mythos::Batch>& batches) {
  UploadBatches(layers_[slot].batches, batches);
}

void Viewport::SetDecals(const int slot,
                         const std::vector<mythos::Batch>& batches) {
  UploadBatches(layers_[slot].decals, batches);
}

void Viewport::UploadBatches(std::vector<GpuBatch>& into,
                             const std::vector<mythos::Batch>& batches) {
  into.clear();
  for (const auto& b : batches) {
    if (b.verts.empty()) continue;
    GpuBatch g;
    g.vao.Create();
    g.vbo.Create();
    g.vao.Bind();
    g.vbo.Upload(b.verts.data(), b.verts.size() * sizeof(mythos::Vertex),
                 gl::kStaticDraw);
    const int stride = sizeof(mythos::Vertex);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, gl::kFloat, gl::kFalse, stride, nullptr);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 3, gl::kFloat, gl::kFalse, stride,
                            reinterpret_cast<const void*>(3 * sizeof(float)));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 2, gl::kFloat, gl::kFalse, stride,
                            reinterpret_cast<const void*>(6 * sizeof(float)));
    g.count = static_cast<int>(b.verts.size());
    g.texture = b.texture;
    g.emissive = b.emissive;
    g.alpha_test = b.alpha_test;
    into.push_back(std::move(g));
  }
  gl::BindVertexArray(0);
}

void Viewport::SetStyle(const int slot, const LayerStyle& style) {
  layers_[slot].style = style;
}

void Viewport::ClearOverlay() {
  lines_.clear();
  lines_top_.clear();
  tris_.clear();
}

void Viewport::Line(const Vec3 a, const Vec3 b, const Rgba c,
                    const bool on_top) {
  auto& v = on_top ? lines_top_ : lines_;
  v.push_back({a.x, a.y, a.z, c.r, c.g, c.b, c.a});
  v.push_back({b.x, b.y, b.z, c.r, c.g, c.b, c.a});
}

// Corner i takes its sign on x, y and z from bits 0, 1 and 2 of i; the 12
// edges join corners that differ in one bit.
void Viewport::WireBox(const Vec3 centre, const Vec3 half, const float yaw,
                       const Rgba c, const bool on_top) {
  const float cs = std::cos(yaw), sn = std::sin(yaw);
  Vec3 p[8];
  for (int i = 0; i < 8; ++i) {
    const float lx = (i & 1 ? 1 : -1) * half.x, ly = (i & 2 ? 1 : -1) * half.y,
                lz = (i & 4 ? 1 : -1) * half.z;
    p[i] = centre + Vec3{lx * cs - lz * sn, ly, lx * sn + lz * cs};
  }
  static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7},
                                   {0, 2}, {1, 3}, {4, 6}, {5, 7},
                                   {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  for (const auto& e : edges) Line(p[e[0]], p[e[1]], c, on_top);
}

void Viewport::Circle(const Vec3 centre, const float radius, const Rgba c,
                      const bool on_top, const int n) {
  for (int i = 0; i < n; ++i) {
    const float a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
    Line(centre + Vec3{std::cos(a0) * radius, 0, std::sin(a0) * radius},
         centre + Vec3{std::cos(a1) * radius, 0, std::sin(a1) * radius}, c,
         on_top);
  }
}

// The head is four lines fanned back from the tip; the side axis avoids a
// vertical direction so the cross product stays well defined.
void Viewport::Arrow(const Vec3 from, const Vec3 to, const Rgba c,
                     const float head) {
  Line(from, to, c, true);
  const Vec3 d = Normalize(to - from);
  const Vec3 side =
      Normalize(Cross(d, std::abs(d.y) > 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0}));
  const Vec3 up = Cross(side, d);
  const Vec3 base = to - d * head;
  for (const Vec3& o : {side * (head * 0.35f), side * (-head * 0.35f),
                        up * (head * 0.35f), up * (-head * 0.35f)})
    Line(to, base + o, c, true);
}

void Viewport::Quad(const Vec3 a, const Vec3 b, const Vec3 c, const Vec3 d,
                    const Rgba col) {
  for (const Vec3& p : {a, b, c, a, c, d})
    tris_.push_back({p.x, p.y, p.z, col.r, col.g, col.b, col.a});
}

void Viewport::SolidBox(const Vec3 centre, const Vec3 half, const float yaw,
                        const Rgba c) {
  const float cs = std::cos(yaw), sn = std::sin(yaw);
  Vec3 p[8];
  for (int i = 0; i < 8; ++i) {
    const float lx = (i & 1 ? 1 : -1) * half.x, ly = (i & 2 ? 1 : -1) * half.y,
                lz = (i & 4 ? 1 : -1) * half.z;
    p[i] = centre + Vec3{lx * cs - lz * sn, ly, lx * sn + lz * cs};
  }
  static const int faces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4},
                                  {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
  for (const auto& f : faces) Quad(p[f[0]], p[f[1]], p[f[2]], p[f[3]], c);
}

// Translucent layers (alpha < 1) are blended; Render draws them after the
// opaque ones.
void Viewport::DrawLayer(const Layer& layer, const Mat4& mvp,
                         const ShadeMode mode, const bool decals) {
  const auto& batches = decals ? layer.decals : layer.batches;
  if (!layer.style.visible || batches.empty()) return;
  world_.Use();
  world_.SetMat4("u_mvp", mvp.m);
  gl::Uniform4f(world_.Uniform("u_tint"), layer.style.tint, layer.style.tint,
                layer.style.tint, layer.style.alpha);
  world_.Set("u_clip_y", layer.style.clip_y);
  world_.Set("u_flat", mode == ShadeMode::kTextured ? 0 : 1);
  world_.Set("u_decal", decals ? 1 : 0);
  world_.Set("u_tex", 0);
  gl::ActiveTexture(gl::kTexture0);
  const bool ghost = layer.style.alpha < 0.999f;
  if (ghost && !decals) {
    gl::Enable(gl::kBlend);
    gl::BlendFunc(gl::kSrcAlpha, gl::kOneMinusSrcAlpha);
  }
  for (const auto& b : batches) {
    world_.Set("u_emissive", b.emissive ? 1 : 0);
    world_.Set("u_alpha_test", b.alpha_test ? 1 : 0);
    gl::BindTexture(gl::kTexture2D, TextureFor(b.texture));
    b.vao.Bind();
    gl::DrawArrays(gl::kTriangles, 0, b.count);
  }
  if (ghost && !decals) gl::Disable(gl::kBlend);
}

void Viewport::DrawColor(const std::vector<ColorVertex>& v, const unsigned mode,
                         const Mat4& mvp, const bool depth_test) {
  if (v.empty()) return;
  color_.Use();
  color_.SetMat4("u_mvp", mvp.m);
  color_vao_.Bind();
  color_vbo_.Upload(v.data(), v.size() * sizeof(ColorVertex), gl::kDynamicDraw);
  gl::EnableVertexAttribArray(0);
  gl::VertexAttribPointer(0, 3, gl::kFloat, gl::kFalse, sizeof(ColorVertex),
                          nullptr);
  gl::EnableVertexAttribArray(1);
  gl::VertexAttribPointer(1, 4, gl::kFloat, gl::kFalse, sizeof(ColorVertex),
                          reinterpret_cast<const void*>(3 * sizeof(float)));
  if (depth_test)
    gl::Enable(gl::kDepthTest);
  else
    gl::Disable(gl::kDepthTest);
  gl::DrawArrays(mode, 0, static_cast<int>(v.size()));
}

// Order: opaque layers and decals, ghost layers and decals, depth-tested lines,
// blended triangles without depth writes, then the on-top lines. Wireframe
// mode applies to the world layers only, never to decals or the overlay.
unsigned Viewport::Render(const OrbitCamera& cam, int w, int h,
                          const ShadeMode mode, const Rgba background) {
  w = std::max(w, 16);
  h = std::max(h, 16);
  target_.Resize(w, h);
  target_.Bind();
  gl::Viewport(0, 0, w, h);
  gl::ClearColor(background.r, background.g, background.b, 1.0f);
  gl::Clear(gl::kColorBufferBit | gl::kDepthBufferBit);
  gl::Enable(gl::kDepthTest);
  gl::DepthFunc(gl::kLequal);
  const Mat4 mvp =
      Multiply(cam.Projection(static_cast<float>(w) / h), cam.View());

  if (mode == ShadeMode::kWireframe && g_polygon_mode)
    g_polygon_mode(kFrontAndBack, kLineMode);
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i < kSlots; ++i) {
      const bool ghost = layers_[i].style.alpha < 0.999f;
      if (ghost == (pass == 1)) DrawLayer(layers_[i], mvp, mode);
    }
    if (mode == ShadeMode::kWireframe && g_polygon_mode)
      g_polygon_mode(kFrontAndBack, kFillMode);
    // Save the state touched by the decal pass, including the offset values.
    constexpr gl::GLenum kDepthWriteMask = 0x0B72;
    constexpr gl::GLenum kPolygonOffsetFactor = 0x8038;
    constexpr gl::GLenum kPolygonOffsetUnits = 0x2A00;
    constexpr gl::GLenum kBlendSrcRgb = 0x80C9, kBlendDstRgb = 0x80C8;
    constexpr gl::GLenum kBlendSrcAlpha = 0x80CB, kBlendDstAlpha = 0x80CA;
    const auto is_enabled =
        reinterpret_cast<gl::GLboolean(MYTHOS_GL_CALL*)(gl::GLenum)>(
            SDL_GL_GetProcAddress("glIsEnabled"));
    const auto get_float =
        reinterpret_cast<void(MYTHOS_GL_CALL*)(gl::GLenum, gl::GLfloat*)>(
            SDL_GL_GetProcAddress("glGetFloatv"));
    const auto blend_separate = reinterpret_cast<void(MYTHOS_GL_CALL*)(
        gl::GLenum, gl::GLenum, gl::GLenum, gl::GLenum)>(
        SDL_GL_GetProcAddress("glBlendFuncSeparate"));
    if (is_enabled && get_float && blend_separate) {
      const bool blend = is_enabled(gl::kBlend);
      const bool offset = is_enabled(gl::kPolygonOffsetFill);
      gl::GLint depth_mask, src_rgb, dst_rgb, src_alpha, dst_alpha;
      gl::GLfloat factor, units;
      gl::GetIntegerv(kDepthWriteMask, &depth_mask);
      gl::GetIntegerv(kBlendSrcRgb, &src_rgb);
      gl::GetIntegerv(kBlendDstRgb, &dst_rgb);
      gl::GetIntegerv(kBlendSrcAlpha, &src_alpha);
      gl::GetIntegerv(kBlendDstAlpha, &dst_alpha);
      get_float(kPolygonOffsetFactor, &factor);
      get_float(kPolygonOffsetUnits, &units);
      gl::Enable(gl::kBlend);
      gl::BlendFunc(gl::kSrcAlpha, gl::kOneMinusSrcAlpha);
      gl::DepthMask(gl::kFalse);
      gl::Enable(gl::kPolygonOffsetFill);
      gl::PolygonOffset(-1.0f, -1.0f);
      for (int i = 0; i < kSlots; ++i) {
        const bool ghost = layers_[i].style.alpha < 0.999f;
        if (ghost == (pass == 1)) DrawLayer(layers_[i], mvp, mode, true);
      }
      gl::PolygonOffset(factor, units);
      if (!offset) gl::Disable(gl::kPolygonOffsetFill);
      gl::DepthMask(static_cast<gl::GLboolean>(depth_mask));
      blend_separate(src_rgb, dst_rgb, src_alpha, dst_alpha);
      if (!blend) gl::Disable(gl::kBlend);
    }
    if (mode == ShadeMode::kWireframe && g_polygon_mode && pass == 0)
      g_polygon_mode(kFrontAndBack, kLineMode);
  }
  if (mode == ShadeMode::kWireframe && g_polygon_mode)
    g_polygon_mode(kFrontAndBack, kFillMode);

  DrawColor(lines_, kLines, mvp, true);
  gl::Enable(gl::kBlend);
  gl::BlendFunc(gl::kSrcAlpha, gl::kOneMinusSrcAlpha);
  if (g_depth_mask) g_depth_mask(0);
  DrawColor(tris_, gl::kTriangles, mvp, true);
  if (g_depth_mask) g_depth_mask(1);
  DrawColor(lines_top_, kLines, mvp, false);
  gl::Disable(gl::kBlend);
  gl::Enable(gl::kDepthTest);
  gl::BindVertexArray(0);

  target_.Resolve();
  mythos::RenderTarget::BindWindow();
  return target_.Color().Id();
}
}
