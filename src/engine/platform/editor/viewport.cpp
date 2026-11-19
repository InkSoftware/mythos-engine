#include "engine/platform/editor/viewport.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/platform/gl/gl_api.h"

namespace mythos::editor {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr gl::GLenum kLines = 0x0001;
constexpr gl::GLenum kFrontAndBack = 0x0408;
constexpr gl::GLenum kLineMode = 0x1B01;
constexpr gl::GLenum kFillMode = 0x1B02;

// Entry points the engine's loader leaves out.
void(MYTHOS_GL_CALL* g_depth_mask)(gl::GLboolean) = nullptr;
void(MYTHOS_GL_CALL* g_polygon_mode)(gl::GLenum, gl::GLenum) = nullptr;

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
uniform vec4 u_tint;      // rgb brightness, a opacity
uniform float u_clip_y;
uniform int u_flat;       // 1: no texture, grey
uniform int u_emissive;
uniform int u_alpha_test;
out vec4 o_color;
void main() {
  if (v_pos.y > u_clip_y) discard;
  vec4 t = u_flat == 1 ? vec4(0.72, 0.72, 0.74, 1.0) : texture(u_tex, v_uv);
  if (u_alpha_test == 1 && t.a < 0.5) discard;
  vec3 n = normalize(v_normal);
  float sun = max(dot(n, normalize(vec3(0.35, 0.85, 0.4))), 0.0);
  float fill = max(dot(n, normalize(vec3(-0.6, 0.3, -0.5))), 0.0);
  float hemi = 0.5 + 0.5 * n.y;
  float light = u_emissive == 1 ? 1.0 : 0.30 + 0.20 * hemi + 0.55 * sun + 0.18 * fill;
  o_color = vec4(t.rgb * light * u_tint.rgb, u_tint.a);
}
)";

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

}  // namespace

// ---------------------------------------------------------------- maths

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

float RayHitsHorizontalPlane(const Ray& r, float height) {
  if (std::abs(r.d.y) < 1e-6f) return -1;
  const float t = (height - r.o.y) / r.d.y;
  return t >= 0 ? t : -1;
}

float RayHitsBox(const Ray& r, Vec3 centre, Vec3 half, float yaw) {
  // Into the box's own frame (turn the ray by -yaw about y), then a slab test.
  const float c = std::cos(-yaw), s = std::sin(-yaw);
  auto turn = [&](Vec3 v) {
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

float RayHitsSphere(const Ray& r, Vec3 centre, float radius) {
  const Vec3 oc = r.o - centre;
  const float b = Dot(oc, r.d), c = Dot(oc, oc) - radius * radius;
  const float disc = b * b - c;
  if (disc < 0) return -1;
  const float t = -b - std::sqrt(disc);
  return t >= 0 ? t : (-b + std::sqrt(disc) >= 0 ? 0.0f : -1.0f);
}

bool NearestOnLine(const Ray& r, Vec3 a, Vec3 da, float* t) {
  // Closest points of two lines: a + t*da and r.o + s*r.d.
  const Vec3 w = a - r.o;
  const float b = Dot(da, r.d), d = Dot(da, w), e = Dot(r.d, w);
  const float den = Dot(da, da) - b * b;  // r.d is a unit vector
  if (std::abs(den) < 1e-6f) return false;
  *t = (b * e - d) / den;
  return true;
}

// ---------------------------------------------------------------- camera

Vec3 OrbitCamera::Forward() const {
  const float cp = std::cos(pitch);
  return {cp * std::cos(heading), -std::sin(pitch), cp * std::sin(heading)};
}

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

Mat4 OrbitCamera::Projection(float aspect) const {
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

Ray OrbitCamera::PickRay(float nx, float ny, float aspect) const {
  const Vec3 f = Forward();
  const Vec3 s = Normalize(
      Cross(f, std::abs(f.y) > 0.9999f ? Vec3{0, 0, 1} : Vec3{0, 1, 0}));
  const Vec3 u = Cross(s, f);
  const float th = std::tan(fov_deg * kPi / 360.0f);
  const Vec3 d = Normalize(f + s * (nx * th * aspect) + u * (ny * th));
  return {Eye(), d};
}

void OrbitCamera::Orbit(float dh, float dp) {
  heading += dh;
  pitch = std::clamp(pitch + dp, -1.5f, 1.5533f);
}

void OrbitCamera::Pan(float dx, float dy, float height_px) {
  const float world_per_px = 2.0f * distance *
                             std::tan(fov_deg * kPi / 360.0f) /
                             std::max(1.0f, height_px);
  const Vec3 f = Forward();
  const Vec3 s = Normalize(
      Cross(f, std::abs(f.y) > 0.9999f ? Vec3{0, 0, 1} : Vec3{0, 1, 0}));
  const Vec3 u = Cross(s, f);
  target = target - s * (dx * world_per_px) + u * (dy * world_per_px);
}

void OrbitCamera::Zoom(float wheel) {
  distance = std::clamp(distance * std::pow(0.88f, wheel), 0.5f, 300.0f);
}

void OrbitCamera::Fly(float forward, float right, float up) {
  const Vec3 f = Forward();
  target = target + f * forward + Right() * right + Vec3{0, up, 0};
}

void OrbitCamera::Look(float dh, float dp) {
  const Vec3 eye = Eye();
  heading += dh;
  pitch = std::clamp(pitch + dp, -1.5f, 1.5533f);
  target = eye + Forward() * distance;
}

void OrbitCamera::FocusOn(Vec3 point, float radius) {
  target = point;
  distance = std::clamp(radius * 3.0f, 2.0f, 80.0f);
}

bool OrbitCamera::Project(Vec3 p, float w, float h, float* sx,
                          float* sy) const {
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

// ---------------------------------------------------------------- viewport

bool Viewport::Init(const mythos::TextureLibrary* textures) {
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

void Viewport::SetLayer(int slot, const std::vector<mythos::Batch>& batches) {
  Layer& layer = layers_[slot];
  layer.batches.clear();
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
    layer.batches.push_back(std::move(g));
  }
  gl::BindVertexArray(0);
}

void Viewport::SetStyle(int slot, const LayerStyle& style) {
  layers_[slot].style = style;
}

void Viewport::ClearOverlay() {
  lines_.clear();
  lines_top_.clear();
  tris_.clear();
}

void Viewport::Line(Vec3 a, Vec3 b, Rgba c, bool on_top) {
  auto& v = on_top ? lines_top_ : lines_;
  v.push_back({a.x, a.y, a.z, c.r, c.g, c.b, c.a});
  v.push_back({b.x, b.y, b.z, c.r, c.g, c.b, c.a});
}

void Viewport::WireBox(Vec3 centre, Vec3 half, float yaw, Rgba c, bool on_top) {
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

void Viewport::Circle(Vec3 centre, float radius, Rgba c, bool on_top, int n) {
  for (int i = 0; i < n; ++i) {
    const float a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
    Line(centre + Vec3{std::cos(a0) * radius, 0, std::sin(a0) * radius},
         centre + Vec3{std::cos(a1) * radius, 0, std::sin(a1) * radius}, c,
         on_top);
  }
}

void Viewport::Arrow(Vec3 from, Vec3 to, Rgba c, float head) {
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

void Viewport::Quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Rgba col) {
  for (const Vec3& p : {a, b, c, a, c, d})
    tris_.push_back({p.x, p.y, p.z, col.r, col.g, col.b, col.a});
}

void Viewport::SolidBox(Vec3 centre, Vec3 half, float yaw, Rgba c) {
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

void Viewport::DrawLayer(const Layer& layer, const Mat4& mvp, ShadeMode mode) {
  if (!layer.style.visible || layer.batches.empty()) return;
  world_.Use();
  world_.SetMat4("u_mvp", mvp.m);
  gl::Uniform4f(world_.Uniform("u_tint"), layer.style.tint, layer.style.tint,
                layer.style.tint, layer.style.alpha);
  world_.Set("u_clip_y", layer.style.clip_y);
  world_.Set("u_flat", mode == ShadeMode::kTextured ? 0 : 1);
  world_.Set("u_tex", 0);
  gl::ActiveTexture(gl::kTexture0);
  const bool ghost = layer.style.alpha < 0.999f;
  if (ghost) {
    gl::Enable(gl::kBlend);
    gl::BlendFunc(gl::kSrcAlpha, gl::kOneMinusSrcAlpha);
  }
  for (const auto& b : layer.batches) {
    world_.Set("u_emissive", b.emissive ? 1 : 0);
    world_.Set("u_alpha_test", b.alpha_test ? 1 : 0);
    gl::BindTexture(gl::kTexture2D, TextureFor(b.texture));
    b.vao.Bind();
    gl::DrawArrays(gl::kTriangles, 0, b.count);
  }
  if (ghost) gl::Disable(gl::kBlend);
}

void Viewport::DrawColor(const std::vector<ColorVertex>& v, unsigned mode,
                         const Mat4& mvp, bool depth_test) {
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

unsigned Viewport::Render(const OrbitCamera& cam, int w, int h, ShadeMode mode,
                          Rgba background) {
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
  for (int pass = 0; pass < 2; ++pass)  // solid layers, then the faint ones
    for (int i = 0; i < kSlots; ++i) {
      const bool ghost = layers_[i].style.alpha < 0.999f;
      if (ghost == (pass == 1)) DrawLayer(layers_[i], mvp, mode);
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

}  // namespace mythos::editor
