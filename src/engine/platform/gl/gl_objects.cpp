#include "engine/platform/gl/gl_objects.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>

namespace mythos {
using gl::AttachShader;
using gl::BindAttribLocation;
using gl::BindBuffer;
using gl::BindFramebuffer;
using gl::BindRenderbuffer;
using gl::BindTexture;
using gl::BindVertexArray;
using gl::BlitFramebuffer;
using gl::BufferData;
using gl::CheckFramebufferStatus;
using gl::CompileShader;
using gl::CreateProgram;
using gl::CreateShader;
using gl::DeleteBuffers;
using gl::DeleteFramebuffers;
using gl::DeleteProgram;
using gl::DeleteRenderbuffers;
using gl::DeleteShader;
using gl::DeleteTextures;
using gl::DeleteVertexArrays;
using gl::FramebufferRenderbuffer;
using gl::FramebufferTexture2D;
using gl::GenBuffers;
using gl::GenFramebuffers;
using gl::GenRenderbuffers;
using gl::GenTextures;
using gl::GenVertexArrays;
using gl::GetIntegerv;
using gl::GetProgramInfoLog;
using gl::GetProgramiv;
using gl::GetShaderInfoLog;
using gl::GetShaderiv;
using gl::GetUniformLocation;
using gl::GLenum;
using gl::GLint;
using gl::GLsizeiptr;
using gl::GLuint;
using gl::kArrayBuffer;
using gl::kClampToEdge;
using gl::kColorAttachment0;
using gl::kColorBufferBit;
using gl::kCompileStatus;
using gl::kDepthAttachment;
using gl::kDepthComponent24;
using gl::kDrawFramebuffer;
using gl::kFalse;
using gl::kFloat;
using gl::kFragmentShader;
using gl::kFramebuffer;
using gl::kFramebufferComplete;
using gl::kLinear;
using gl::kLinkStatus;
using gl::kMaxSamples;
using gl::kNearest;
using gl::kNearestFilter;
using gl::kR32F;
using gl::kReadFramebuffer;
using gl::kRed;
using gl::kRenderbuffer;
using gl::kRgba;
using gl::kRgba8;
using gl::kTexture2D;
using gl::kTextureCubeMap;
using gl::kTextureCubeMapPositiveX;
using gl::kTextureMagFilter;
using gl::kTextureMinFilter;
using gl::kTextureWrapS;
using gl::kTextureWrapT;
using gl::kUnsignedByte;
using gl::kVertexShader;
using gl::LinkProgram;
using gl::RenderbufferStorage;
using gl::RenderbufferStorageMultisample;
using gl::ShaderSource;
using gl::TexImage2D;
using gl::TexParameteri;
using gl::Uniform1f;
using gl::Uniform1fv;
using gl::Uniform1i;
using gl::Uniform2f;
using gl::Uniform3f;
using gl::Uniform3fv;
using gl::UniformMatrix4fv;
using gl::UseProgram;
using gl::Viewport;

namespace {
constexpr int kCubeFaces = 6;
constexpr GLenum kDepthFormat = kDepthComponent24;
constexpr size_t kInfoLogBytes = 1024;
}

void GlBuffer::Create() {
  if (!id_) GenBuffers(1, &id_);
}

void GlBuffer::Reset() {
  if (id_ && gl::Loaded()) DeleteBuffers(1, &id_);
  id_ = 0;
}

void GlBuffer::Upload(const void* const data, const size_t bytes,
                      const gl::GLenum usage) {
  Create();
  BindBuffer(kArrayBuffer, id_);
  BufferData(kArrayBuffer, static_cast<GLsizeiptr>(bytes), data, usage);
}

void GlVertexArray::Create() {
  if (!id_) GenVertexArrays(1, &id_);
}

void GlVertexArray::Reset() {
  if (id_ && gl::Loaded()) DeleteVertexArrays(1, &id_);
  id_ = 0;
}

void GlVertexArray::Bind() const { BindVertexArray(id_); }

void GlTexture::Create() {
  if (!id_) GenTextures(1, &id_);
}

void GlTexture::Reset() {
  if (id_ && gl::Loaded()) DeleteTextures(1, &id_);
  id_ = 0;
}

void GlTexture::Bind() const { BindTexture(kTexture2D, id_); }

void GlTexture::SetParams(const GLint min_filter, const GLint mag_filter,
                          const GLint wrap) const {
  Bind();
  TexParameteri(kTexture2D, kTextureMinFilter, min_filter);
  TexParameteri(kTexture2D, kTextureMagFilter, mag_filter);
  TexParameteri(kTexture2D, kTextureWrapS, wrap);
  TexParameteri(kTexture2D, kTextureWrapT, wrap);
}

void GlTexture::Allocate(const int w, const int h,
                         const void* const rgba_pixels,
                         const gl::GLenum internal_format) const {
  Bind();
  TexImage2D(kTexture2D, 0, static_cast<gl::GLint>(internal_format), w, h, 0,
             kRgba, kUnsignedByte, rgba_pixels);
}

void SurfaceToRgbaFlipped(const Surface& s, std::vector<uint8_t>& out) {
  out.resize(static_cast<size_t>(s.w) * static_cast<size_t>(s.h) * 4);
  for (int y = 0; y < s.h; ++y) {
    const uint32_t* src =
        &s.px[static_cast<size_t>(s.h - 1 - y) * static_cast<size_t>(s.w)];
    uint8_t* dst = &out[static_cast<size_t>(y) * static_cast<size_t>(s.w) * 4];
    for (int x = 0; x < s.w; ++x, dst += 4) {
      const uint32_t p = src[x];
      dst[0] = static_cast<uint8_t>((p >> 16) & 0xFF);
      dst[1] = static_cast<uint8_t>((p >> 8) & 0xFF);
      dst[2] = static_cast<uint8_t>(p & 0xFF);
      dst[3] = static_cast<uint8_t>((p >> 24) & 0xFF);
    }
  }
}

void RenderTarget::Create(const int w, const int h) {
  Reset();
  w_ = w;
  h_ = h;
  color_.Create();
  color_.Allocate(w, h);
  color_.SetParams(kLinear, kLinear, kClampToEdge);
  // The depth is a texture (so a pass can read it), not a renderbuffer.
  depth_tex_.Create();
  depth_tex_.Bind();
  TexImage2D(kTexture2D, 0, static_cast<GLint>(kDepthFormat), w, h, 0,
             gl::kDepthComponent, gl::kUnsignedInt, nullptr);
  depth_tex_.SetParams(kNearest, kNearest, kClampToEdge);
  GenFramebuffers(1, &fbo_);
  BindFramebuffer(kFramebuffer, fbo_);
  FramebufferTexture2D(kFramebuffer, kColorAttachment0, kTexture2D, color_.Id(),
                       0);
  FramebufferTexture2D(kFramebuffer, kDepthAttachment, kTexture2D,
                       depth_tex_.Id(), 0);
  BindFramebuffer(kFramebuffer, 0);
}

int RenderTarget::MaxSamples() {
  static int cached = -1;
  if (cached < 0 || !gl::Loaded()) {
    GLint v = 0;
    if (gl::Loaded()) GetIntegerv(kMaxSamples, &v);
    cached = std::max(0, static_cast<int>(v));
  }
  return cached;
}

void RenderTarget::DropMultisample() {
  if (gl::Loaded()) {
    if (ms_fbo_) DeleteFramebuffers(1, &ms_fbo_);
    if (ms_color_) DeleteRenderbuffers(1, &ms_color_);
    if (ms_depth_) DeleteRenderbuffers(1, &ms_depth_);
  }
  ms_fbo_ = ms_color_ = ms_depth_ = 0;
}

void RenderTarget::RebuildMultisample() {
  DropMultisample();
  if (samples_ < 2 || w_ <= 0 || h_ <= 0) return;
  GenRenderbuffers(1, &ms_color_);
  BindRenderbuffer(kRenderbuffer, ms_color_);
  RenderbufferStorageMultisample(kRenderbuffer, samples_, kRgba8, w_, h_);
  GenRenderbuffers(1, &ms_depth_);
  BindRenderbuffer(kRenderbuffer, ms_depth_);
  RenderbufferStorageMultisample(kRenderbuffer, samples_, kDepthFormat, w_, h_);
  GenFramebuffers(1, &ms_fbo_);
  BindFramebuffer(kFramebuffer, ms_fbo_);
  FramebufferRenderbuffer(kFramebuffer, kColorAttachment0, kRenderbuffer,
                          ms_color_);
  FramebufferRenderbuffer(kFramebuffer, kDepthAttachment, kRenderbuffer,
                          ms_depth_);
  const bool ok = CheckFramebufferStatus(kFramebuffer) == kFramebufferComplete;
  BindFramebuffer(kFramebuffer, 0);
  if (!ok) {
    SDL_Log("multisampled framebuffer incomplete: anti-aliasing off");
    DropMultisample();
    samples_ = 0;
  }
}

void RenderTarget::SetSamples(int n) {
  n = n < 2 ? 0 : std::min(n, MaxSamples());
  if (n < 2) n = 0;
  if (n == samples_) return;
  samples_ = n;
  RebuildMultisample();
}

void RenderTarget::Resolve() const {
  if (!ms_fbo_) return;
  BindFramebuffer(kReadFramebuffer, ms_fbo_);
  BindFramebuffer(kDrawFramebuffer, fbo_);
  BlitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_,
                  kColorBufferBit | (resolve_depth_ ? gl::kDepthBufferBit : 0),
                  kNearestFilter);
  BindFramebuffer(kFramebuffer, 0);
}

void RenderTarget::Reset() {
  DropMultisample();
  samples_ = 0;
  if (gl::Loaded()) {
    if (fbo_) DeleteFramebuffers(1, &fbo_);
  }
  fbo_ = 0;
  color_.Reset();
  depth_tex_.Reset();
  w_ = h_ = 0;
}

void RenderTarget::Resize(const int w, const int h) {
  if (w == w_ && h == h_) return;
  w_ = w;
  h_ = h;
  color_.Allocate(w, h);
  depth_tex_.Bind();
  TexImage2D(kTexture2D, 0, static_cast<GLint>(kDepthFormat), w, h, 0,
             gl::kDepthComponent, gl::kUnsignedInt, nullptr);
  RebuildMultisample();
}

void RenderTarget::Bind() const {
  BindFramebuffer(kFramebuffer, ms_fbo_ ? ms_fbo_ : fbo_);
}

void RenderTarget::BindWindow() { BindFramebuffer(kFramebuffer, 0); }

void DepthBuffer::Create(const int w, const int h) {
  if (!id_) GenRenderbuffers(1, &id_);
  BindRenderbuffer(kRenderbuffer, id_);
  RenderbufferStorage(kRenderbuffer, kDepthFormat, w, h);
}

void DepthBuffer::Reset() {
  if (id_ && gl::Loaded()) DeleteRenderbuffers(1, &id_);
  id_ = 0;
}

void ShadowCube::Create(const int size, const DepthBuffer& depth) {
  Reset();
  size_ = size;
  GenTextures(1, &tex_);
  BindTexture(kTextureCubeMap, tex_);
  for (int face = 0; face < kCubeFaces; ++face) {
    TexImage2D(kTextureCubeMapPositiveX + static_cast<GLenum>(face), 0, kR32F,
               size, size, 0, kRed, kFloat, nullptr);
  }
  TexParameteri(kTextureCubeMap, kTextureMinFilter, kNearest);
  TexParameteri(kTextureCubeMap, kTextureMagFilter, kNearest);
  TexParameteri(kTextureCubeMap, kTextureWrapS, kClampToEdge);
  TexParameteri(kTextureCubeMap, kTextureWrapT, kClampToEdge);
  GenFramebuffers(1, &fbo_);
  BindFramebuffer(kFramebuffer, fbo_);
  FramebufferRenderbuffer(kFramebuffer, kDepthAttachment, kRenderbuffer,
                          depth.Id());
  FramebufferTexture2D(kFramebuffer, kColorAttachment0,
                       kTextureCubeMapPositiveX, tex_, 0);
  if (CheckFramebufferStatus(kFramebuffer) != kFramebufferComplete) {
    SDL_Log("shadow cube framebuffer incomplete: shadows off");
    BindFramebuffer(kFramebuffer, 0);
    Reset();
    return;
  }
  BindFramebuffer(kFramebuffer, 0);
}

void ShadowCube::Reset() {
  if (gl::Loaded()) {
    if (fbo_) DeleteFramebuffers(1, &fbo_);
    if (tex_) DeleteTextures(1, &tex_);
  }
  tex_ = fbo_ = 0;
  size_ = 0;
}

void ReflectionCube::Create(const int size, const DepthBuffer& depth) {
  Reset();
  size_ = size;
  GenTextures(1, &tex_);
  BindTexture(kTextureCubeMap, tex_);
  for (int face = 0; face < kCubeFaces; ++face) {
    TexImage2D(kTextureCubeMapPositiveX + static_cast<GLenum>(face), 0, kRgba8,
               size, size, 0, kRgba, kUnsignedByte, nullptr);
  }
  TexParameteri(kTextureCubeMap, kTextureMinFilter, gl::kLinearMipmapLinear);
  TexParameteri(kTextureCubeMap, kTextureMagFilter, kLinear);
  TexParameteri(kTextureCubeMap, kTextureWrapS, kClampToEdge);
  TexParameteri(kTextureCubeMap, kTextureWrapT, kClampToEdge);
  GenFramebuffers(1, &fbo_);
  BindFramebuffer(kFramebuffer, fbo_);
  FramebufferRenderbuffer(kFramebuffer, kDepthAttachment, kRenderbuffer,
                          depth.Id());
  FramebufferTexture2D(kFramebuffer, kColorAttachment0,
                       kTextureCubeMapPositiveX, tex_, 0);
  if (CheckFramebufferStatus(kFramebuffer) != kFramebufferComplete) {
    SDL_Log("reflection cube framebuffer incomplete: reflections off");
    BindFramebuffer(kFramebuffer, 0);
    Reset();
    return;
  }
  BindFramebuffer(kFramebuffer, 0);
}

void ReflectionCube::Reset() {
  if (gl::Loaded()) {
    if (fbo_) DeleteFramebuffers(1, &fbo_);
    if (tex_) DeleteTextures(1, &tex_);
  }
  tex_ = fbo_ = 0;
  size_ = 0;
}

void ReflectionCube::BindFace(const int face) const {
  BindFramebuffer(kFramebuffer, fbo_);
  FramebufferTexture2D(kFramebuffer, kColorAttachment0,
                       kTextureCubeMapPositiveX + static_cast<GLenum>(face),
                       tex_, 0);
  Viewport(0, 0, size_, size_);
}

void ReflectionCube::BuildMips() const {
  BindTexture(kTextureCubeMap, tex_);
  gl::GenerateMipmap(kTextureCubeMap);
}

int ReflectionCube::Levels() const {
  int levels = 1;
  for (int s = size_; s > 1; s /= 2) ++levels;
  return levels;
}

void ShadowCube::BindFace(const int face) const {
  BindFramebuffer(kFramebuffer, fbo_);
  FramebufferTexture2D(kFramebuffer, kColorAttachment0,
                       kTextureCubeMapPositiveX + static_cast<GLenum>(face),
                       tex_, 0);
  Viewport(0, 0, size_, size_);
}

bool GlProgram::Build(const char* const vertex_src,
                      const char* const fragment_src,
                      const std::initializer_list<Attrib> attribs,
                      const char* const fragment_extra) {
  Reset();
  const auto compile = [](const GLenum type, const char* const src,
                          const char* const extra) -> GLuint {
    const GLuint shader = CreateShader(type);
    const char* sources[2] = {src, extra};
    ShaderSource(shader, extra ? 2 : 1, sources, nullptr);
    CompileShader(shader);
    GLint ok = 0;
    GetShaderiv(shader, kCompileStatus, &ok);
    if (!ok) {
      char log[kInfoLogBytes] = {0};
      GetShaderInfoLog(shader, sizeof(log), nullptr, log);
      SDL_Log("shader compile error: %s", log);
    }
    return shader;
  };
  const GLuint vertex_shader = compile(kVertexShader, vertex_src, nullptr);
  const GLuint fragment_shader =
      compile(kFragmentShader, fragment_src, fragment_extra);
  const GLuint program = CreateProgram();
  AttachShader(program, vertex_shader);
  AttachShader(program, fragment_shader);
  for (const auto& [location, name] : attribs) {
    BindAttribLocation(program, location, name);
  }
  LinkProgram(program);
  GLint linked = 0;
  GetProgramiv(program, kLinkStatus, &linked);
  if (!linked) {
    char log[kInfoLogBytes] = {0};
    GetProgramInfoLog(program, sizeof(log), nullptr, log);
    SDL_Log("program link error: %s", log);
  }
  DeleteShader(vertex_shader);
  DeleteShader(fragment_shader);
  if (!linked) {
    DeleteProgram(program);
    return false;
  }
  id_ = program;
  return true;
}

void GlProgram::Reset() {
  if (id_ && gl::Loaded()) DeleteProgram(id_);
  id_ = 0;
  uniforms_.clear();
}

void GlProgram::Use() const { UseProgram(id_); }

GLint GlProgram::Uniform(const std::string_view name) const {
  const auto it = uniforms_.find(name);
  if (it != uniforms_.end()) return it->second;
  const GLint loc = GetUniformLocation(id_, std::string(name).c_str());
  uniforms_.emplace(std::string(name), loc);
  return loc;
}

void GlProgram::Set(const std::string_view n, const int v) const {
  Uniform1i(Uniform(n), v);
}

void GlProgram::Set(const std::string_view n, const float v) const {
  Uniform1f(Uniform(n), v);
}

void GlProgram::Set(const std::string_view n, const float a,
                    const float b) const {
  Uniform2f(Uniform(n), a, b);
}

void GlProgram::Set(const std::string_view n, const float a, const float b,
                    const float c) const {
  Uniform3f(Uniform(n), a, b, c);
}

void GlProgram::SetMat4(const std::string_view n, const float* const m) const {
  UniformMatrix4fv(Uniform(n), 1, kFalse, m);
}
}
