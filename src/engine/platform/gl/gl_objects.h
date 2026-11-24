#ifndef MYTHOS_ENGINE_PLATFORM_GL_GL_OBJECTS_H_
#define MYTHOS_ENGINE_PLATFORM_GL_GL_OBJECTS_H_

#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/gfx/surface.h"
#include "engine/platform/gl/gl_api.h"

namespace mythos {
// RAII wrappers over GL objects. Each is move-only, creates its name lazily and
// deletes it on Reset() or destruction, skipping the delete when the GL
// functions are already unloaded. All calls need the context current.
//
// A vertex buffer object.
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

  void Create();

  void Reset();

  [[nodiscard]] gl::GLuint Id() const { return id_; }

  // Creates the buffer if needed, binds it to GL_ARRAY_BUFFER and replaces its
  // contents.
  void Upload(const void* data, size_t bytes, gl::GLenum usage);

 private:
  gl::GLuint id_ = 0;
};

// A vertex array object.
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

  // Binds the framebuffer to draw into (the multisampled one when active).
  void Bind() const;

  [[nodiscard]] gl::GLuint Id() const { return id_; }

 private:
  gl::GLuint id_ = 0;
};

// A 2D texture.
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

  [[nodiscard]] gl::GLuint Id() const { return id_; }

  // Binds the texture and sets its filters and a wrap mode for both axes.
  void SetParams(gl::GLint min_filter, gl::GLint mag_filter,
                 gl::GLint wrap) const;

  // Binds the texture and (re)allocates level 0 as w x h RGBA/UNSIGNED_BYTE
  // with the given internal format, filled from `rgba_pixels` or left
  // undefined.
  void Allocate(int w, int h, const void* rgba_pixels = nullptr,
                gl::GLenum internal_format = gl::kRgba8) const;

 private:
  gl::GLuint id_ = 0;
};

// Converts a BGRA-order `Surface` (0xAARRGGBB pixels) to RGBA bytes with the
// rows flipped vertically, as GL expects the bottom row first.
void SurfaceToRgbaFlipped(const Surface& s, std::vector<uint8_t>& out);

// A color texture plus depth renderbuffer framebuffer, optionally backed by a
// multisampled framebuffer. Draw calls go through Bind(); Resolve() copies the
// multisampled image into the texture, which Color() exposes for sampling.
class RenderTarget {
 public:
  RenderTarget() = default;

  ~RenderTarget() { Reset(); }

  RenderTarget(const RenderTarget&) = delete;

  RenderTarget& operator=(const RenderTarget&) = delete;

  // Allocates the target at w x h, replacing any previous one.
  void Create(int w, int h);

  void Reset();

  // Reallocates for a new size (keeps the sample count); no-op if unchanged.
  void Resize(int w, int h);

  // Sets the MSAA sample count, clamped to MaxSamples(); under 2 turns it off.
  // If the multisampled framebuffer is incomplete, falls back to none.
  void SetSamples(int n);

  // Largest sample count the driver supports (cached; 0 without a context).
  static int MaxSamples();

  // Blits the multisampled image into the color texture; no-op without MSAA.
  void Resolve() const;

  // Also copies the depth into `DepthTexture()` when resolving (off by
  // default: the blit costs). The depth texture is always attached, so with
  // no MSAA the scene is drawn straight into it.
  void SetResolveDepth(bool on) { resolve_depth_ = on; }
  [[nodiscard]] const GlTexture& DepthTexture() const { return depth_tex_; }

  void Bind() const;

  // Binds the window's default framebuffer.
  static void BindWindow();

  [[nodiscard]] int Width() const { return w_; }
  [[nodiscard]] int Height() const { return h_; }
  [[nodiscard]] const GlTexture& Color() const { return color_; }

 private:
  GlTexture color_;
  GlTexture depth_tex_;
  bool resolve_depth_ = false;

  void RebuildMultisample();

  void DropMultisample();

  gl::GLuint fbo_ = 0;
  gl::GLuint ms_color_ = 0, ms_depth_ = 0, ms_fbo_ = 0;
  int samples_ = 0;
  int w_ = 0, h_ = 0;
};

// A depth renderbuffer, shared by shadow cubes of the same size.
class DepthBuffer {
 public:
  DepthBuffer() = default;

  ~DepthBuffer() { Reset(); }

  DepthBuffer(const DepthBuffer&) = delete;

  DepthBuffer& operator=(const DepthBuffer&) = delete;

  void Create(int w, int h);

  void Reset();

  [[nodiscard]] gl::GLuint Id() const { return id_; }

 private:
  gl::GLuint id_ = 0;
};

// A cube map of the scene seen from a point (RGBA8, with mip levels for blurry
// reflections of rough surfaces) and a framebuffer to draw its faces into.
// Uses an external depth buffer of the same size, which must outlive it.
class ReflectionCube {
 public:
  ReflectionCube() = default;

  ~ReflectionCube() { Reset(); }

  ReflectionCube(const ReflectionCube&) = delete;

  ReflectionCube& operator=(const ReflectionCube&) = delete;

  void Create(int size, const DepthBuffer& depth);

  void Reset();

  // Binds the framebuffer with face 0..5 (+X, -X, +Y, -Y, +Z, -Z) as the
  // colour attachment and sets the viewport to the whole face.
  void BindFace(int face) const;

  // Builds the mip chain after all six faces are drawn.
  void BuildMips() const;

  [[nodiscard]] gl::GLuint Texture() const { return tex_; }
  [[nodiscard]] int Size() const { return size_; }
  [[nodiscard]] bool Valid() const { return fbo_ != 0; }
  // Number of mip levels (the highest level index is one less).
  [[nodiscard]] int Levels() const;

 private:
  gl::GLuint tex_ = 0, fbo_ = 0;
  int size_ = 0;
};

// A cube map of R32F distances with a framebuffer; a face is chosen with
// BindFace(). Uses an external depth buffer, which must outlive it and match
// its size.
class ShadowCube {
 public:
  ShadowCube() = default;

  ~ShadowCube() { Reset(); }

  ShadowCube(const ShadowCube&) = delete;

  ShadowCube& operator=(const ShadowCube&) = delete;

  // Allocates six size x size faces. On an incomplete framebuffer it logs and
  // resets, so Valid() is false.
  void Create(int size, const DepthBuffer& depth);

  void Reset();

  // Binds the framebuffer with cube face 0..5 (+X, -X, +Y, -Y, +Z, -Z) as the
  // color attachment and sets the viewport to the whole face.
  void BindFace(int face) const;

  [[nodiscard]] gl::GLuint Texture() const { return tex_; }
  [[nodiscard]] int Size() const { return size_; }
  [[nodiscard]] bool Valid() const { return fbo_ != 0; }

 private:
  gl::GLuint tex_ = 0, fbo_ = 0;
  int size_ = 0;
};

// A linked shader program with name-to-location caching for uniforms. Setting a
// uniform that the program does not have (location -1) is silently ignored by
// GL.
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

  // Compiles and links. `attribs` pairs a location with an attribute name,
  // bound before linking. `fragment_extra`, if given, is a second source string
  // appended to the fragment shader. Compile and link errors are logged;
  // returns false (leaving the program invalid) on a link failure.
  bool Build(const char* vertex_src, const char* fragment_src,
             std::initializer_list<Attrib> attribs,
             const char* fragment_extra = nullptr);

  void Reset();

  bool Valid() const { return id_ != 0; }

  // Makes this the current program.
  void Use() const;

  // Location of a uniform, cached after the first lookup.
  gl::GLint Uniform(std::string_view name) const;

  void Set(std::string_view name, int v) const;

  void Set(std::string_view name, float v) const;

  void Set(std::string_view name, float a, float b) const;

  void Set(std::string_view name, float a, float b, float c) const;

  // Sets a 4x4 matrix from 16 floats in column-major order.
  void SetMat4(std::string_view name, const float* m) const;

 private:
  // Transparent hash so the cache can be searched by string_view without
  // allocating.
  struct StringHash {
    using is_transparent = void;

    size_t operator()(const std::string_view s) const {
      return std::hash<std::string_view>{}(s);
    }
  };

  gl::GLuint id_ = 0;
  mutable std::unordered_map<std::string, gl::GLint, StringHash,
                             std::equal_to<>>
      uniforms_;
};
}

#endif
