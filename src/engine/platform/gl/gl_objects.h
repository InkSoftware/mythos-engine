#ifndef MYTHOS_ENGINE_PLATFORM_GL_GL_OBJECTS_H_
#define MYTHOS_ENGINE_PLATFORM_GL_GL_OBJECTS_H_
// RAII wrappers for the OpenGL objects the renderer owns. Each is move-only and
// releases its object on destruction. They are safe to outlive the GL context:
// Window::close() unloads the GL functions, and the destructors skip the delete
// call when none are loaded.

#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/gfx/surface.h"
#include "engine/platform/gl/gl_api.h"

namespace mythos {

class GlBuffer {
 public:
  GlBuffer() = default;
  ~GlBuffer() { Reset(); }
  GlBuffer(GlBuffer&& o) noexcept : id_(std::exchange(o.id_, 0)) {}
  GlBuffer& operator=(GlBuffer&& o) noexcept {
    if (this != &o) {
      Reset();
      id_ = std::exchange(o.id_, 0);
    }
    return *this;
  }
  GlBuffer(const GlBuffer&) = delete;
  GlBuffer& operator=(const GlBuffer&) = delete;

  void Create();  // no-op when already created
  void Reset();
  gl::GLuint Id() const { return id_; }
  // Binds to GL_ARRAY_BUFFER and (re)allocates it with the data.
  void Upload(const void* data, size_t bytes, gl::GLenum usage);

 private:
  gl::GLuint id_ = 0;
};

class GlVertexArray {
 public:
  GlVertexArray() = default;
  ~GlVertexArray() { Reset(); }
  GlVertexArray(GlVertexArray&& o) noexcept : id_(std::exchange(o.id_, 0)) {}
  GlVertexArray& operator=(GlVertexArray&& o) noexcept {
    if (this != &o) {
      Reset();
      id_ = std::exchange(o.id_, 0);
    }
    return *this;
  }
  GlVertexArray(const GlVertexArray&) = delete;
  GlVertexArray& operator=(const GlVertexArray&) = delete;

  void Create();
  void Reset();
  void Bind() const;
  gl::GLuint Id() const { return id_; }

 private:
  gl::GLuint id_ = 0;
};

class GlTexture {
 public:
  GlTexture() = default;
  ~GlTexture() { Reset(); }
  GlTexture(GlTexture&& o) noexcept : id_(std::exchange(o.id_, 0)) {}
  GlTexture& operator=(GlTexture&& o) noexcept {
    if (this != &o) {
      Reset();
      id_ = std::exchange(o.id_, 0);
    }
    return *this;
  }
  GlTexture(const GlTexture&) = delete;
  GlTexture& operator=(const GlTexture&) = delete;

  void Create();
  void Reset();
  void Bind() const;
  gl::GLuint Id() const { return id_; }

  // Sampler state for the bound-to-this texture.
  void SetParams(gl::GLint min_filter, gl::GLint mag_filter,
                 gl::GLint wrap) const;
  // Allocates storage of w x h with optional pixels (null = uninitialised). The
  // format is RGBA8, or SRGB8_ALPHA8 for colour art, which the hardware then
  // decodes to linear when sampling.
  void Allocate(int w, int h, const void* rgba_pixels = nullptr,
                gl::GLenum internal_format = gl::kRgba8) const;

 private:
  gl::GLuint id_ = 0;
};

// Converts a Surface (0xAARRGGBB, row 0 = top) to RGBA bytes with row 0 =
// bottom, as GL wants.
void SurfaceToRgbaFlipped(const Surface& s, std::vector<uint8_t>& out);

// An offscreen colour texture + depth buffer.
class RenderTarget {
 public:
  RenderTarget() = default;
  ~RenderTarget() { Reset(); }
  RenderTarget(const RenderTarget&) = delete;
  RenderTarget& operator=(const RenderTarget&) = delete;

  void Create(int w, int h);
  void Reset();
  // Reallocates for a new size; does nothing when the size is unchanged.
  void Resize(int w, int h);
  // Multisampling: n > 1 draws into a multisampled buffer (clamped to what the
  // GPU offers, 0 / 1 = none); resolve() then copies it into color(), which is
  // what to sample.
  void SetSamples(int n);
  static int MaxSamples();
  void Resolve() const;
  void Bind() const;         // draw into this target
  static void BindWindow();  // draw into the default framebuffer
  int Width() const { return w_; }
  int Height() const { return h_; }
  const GlTexture& Color() const { return color_; }

 private:
  GlTexture color_;
  void RebuildMultisample();
  void DropMultisample();
  gl::GLuint depth_ = 0, fbo_ = 0;
  gl::GLuint ms_color_ = 0, ms_depth_ = 0, ms_fbo_ = 0;
  int samples_ = 0;
  int w_ = 0, h_ = 0;
};

// A depth renderbuffer, to share between render targets of the same size that
// are never drawn into at once (the shadow cubes, drawn one face after
// another).
class DepthBuffer {
 public:
  DepthBuffer() = default;
  ~DepthBuffer() { Reset(); }
  DepthBuffer(const DepthBuffer&) = delete;
  DepthBuffer& operator=(const DepthBuffer&) = delete;

  void Create(int w, int h);  // replaces any earlier storage
  void Reset();
  gl::GLuint Id() const { return id_; }

 private:
  gl::GLuint id_ = 0;
};

// A cube map of linear distances (one float per texel), drawn with a shared
// depth buffer: the shadow map of one point light. bindFace() makes a face the
// draw target; texture() is sampled as a samplerCube.
class ShadowCube {
 public:
  ShadowCube() = default;
  ~ShadowCube() { Reset(); }
  ShadowCube(const ShadowCube&) = delete;
  ShadowCube& operator=(const ShadowCube&) = delete;

  void Create(int size, const DepthBuffer& depth);
  void Reset();
  void BindFace(
      int face) const;  // 0..5 = +X -X +Y -Y +Z -Z; also sets the viewport
  gl::GLuint Texture() const { return tex_; }
  int Size() const { return size_; }
  bool Valid() const { return fbo_ != 0; }

 private:
  gl::GLuint tex_ = 0, fbo_ = 0;
  int size_ = 0;
};

// A linked shader program with named uniform setters (locations are looked up
// once and cached). Call use() before the setters.
class GlProgram {
 public:
  using Attrib = std::pair<gl::GLuint, const char*>;

  GlProgram() = default;
  ~GlProgram() { Reset(); }
  GlProgram(GlProgram&& o) noexcept
      : id_(std::exchange(o.id_, 0)), uniforms_(std::move(o.uniforms_)) {}
  GlProgram& operator=(GlProgram&& o) noexcept {
    if (this != &o) {
      Reset();
      id_ = std::exchange(o.id_, 0);
      uniforms_ = std::move(o.uniforms_);
    }
    return *this;
  }
  GlProgram(const GlProgram&) = delete;
  GlProgram& operator=(const GlProgram&) = delete;

  // Compiles and links; fragmentExtra (may be null) is appended to the fragment
  // shader. Attribute names are bound to the given locations. Logs errors and
  // returns false on failure.
  bool Build(const char* vertex_src, const char* fragment_src,
             std::initializer_list<Attrib> attribs,
             const char* fragment_extra = nullptr);
  void Reset();
  bool Valid() const { return id_ != 0; }
  void Use() const;

  gl::GLint Uniform(std::string_view name) const;  // -1 when absent
  void Set(std::string_view name, int v) const;
  void Set(std::string_view name, float v) const;
  void Set(std::string_view name, float a, float b) const;
  void Set(std::string_view name, float a, float b, float c) const;
  void SetMat4(std::string_view name, const float* m) const;

 private:
  struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const {
      return std::hash<std::string_view>{}(s);
    }
  };
  gl::GLuint id_ = 0;
  mutable std::unordered_map<std::string, gl::GLint, StringHash,
                             std::equal_to<>>
      uniforms_;
};

}  // namespace mythos

#endif  // MYTHOS_ENGINE_PLATFORM_GL_GL_OBJECTS_H_
