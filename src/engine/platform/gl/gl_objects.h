#pragma once
// RAII wrappers for the OpenGL objects the renderer owns. Each is move-only and releases its
// object on destruction. They are safe to outlive the GL context: Window::close() unloads the
// GL functions, and the destructors skip the delete call when none are loaded.

#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/platform/gl/gl_api.h"
#include "engine/gfx/surface.h"

namespace mythos {

class GlBuffer {
public:
    GlBuffer() = default;
    ~GlBuffer() { reset(); }
    GlBuffer(GlBuffer&& o) noexcept : id_(std::exchange(o.id_, 0)) {}
    GlBuffer& operator=(GlBuffer&& o) noexcept {
        if (this != &o) {
            reset();
            id_ = std::exchange(o.id_, 0);
        }
        return *this;
    }
    GlBuffer(const GlBuffer&) = delete;
    GlBuffer& operator=(const GlBuffer&) = delete;

    void create();  // no-op when already created
    void reset();
    gl::GLuint id() const { return id_; }
    // Binds to GL_ARRAY_BUFFER and (re)allocates it with the data.
    void upload(const void* data, size_t bytes, gl::GLenum usage);

private:
    gl::GLuint id_ = 0;
};

class GlVertexArray {
public:
    GlVertexArray() = default;
    ~GlVertexArray() { reset(); }
    GlVertexArray(GlVertexArray&& o) noexcept : id_(std::exchange(o.id_, 0)) {}
    GlVertexArray& operator=(GlVertexArray&& o) noexcept {
        if (this != &o) {
            reset();
            id_ = std::exchange(o.id_, 0);
        }
        return *this;
    }
    GlVertexArray(const GlVertexArray&) = delete;
    GlVertexArray& operator=(const GlVertexArray&) = delete;

    void create();
    void reset();
    void bind() const;
    gl::GLuint id() const { return id_; }

private:
    gl::GLuint id_ = 0;
};

class GlTexture {
public:
    GlTexture() = default;
    ~GlTexture() { reset(); }
    GlTexture(GlTexture&& o) noexcept : id_(std::exchange(o.id_, 0)) {}
    GlTexture& operator=(GlTexture&& o) noexcept {
        if (this != &o) {
            reset();
            id_ = std::exchange(o.id_, 0);
        }
        return *this;
    }
    GlTexture(const GlTexture&) = delete;
    GlTexture& operator=(const GlTexture&) = delete;

    void create();
    void reset();
    void bind() const;
    gl::GLuint id() const { return id_; }

    // Sampler state for the bound-to-this texture.
    void setParams(gl::GLint minFilter, gl::GLint magFilter, gl::GLint wrap);
    // Allocates storage of w x h with optional pixels (null = uninitialised). The format is RGBA8,
    // or SRGB8_ALPHA8 for colour art, which the hardware then decodes to linear when sampling.
    void allocate(int w, int h, const void* rgbaPixels = nullptr, gl::GLenum internalFormat = gl::RGBA8);

private:
    gl::GLuint id_ = 0;
};

// Converts a Surface (0xAARRGGBB, row 0 = top) to RGBA bytes with row 0 = bottom, as GL wants.
void surfaceToRgbaFlipped(const Surface& s, std::vector<uint8_t>& out);

// An offscreen colour texture + depth buffer.
class RenderTarget {
public:
    RenderTarget() = default;
    ~RenderTarget() { reset(); }
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    void create(int w, int h);
    void reset();
    // Reallocates for a new size; does nothing when the size is unchanged.
    void resize(int w, int h);
    // Multisampling: n > 1 draws into a multisampled buffer (clamped to what the GPU offers, 0 / 1
    // = none); resolve() then copies it into color(), which is what to sample.
    void setSamples(int n);
    int samples() const { return samples_; }
    static int maxSamples();
    void resolve() const;
    void bind() const;  // draw into this target
    static void bindWindow();  // draw into the default framebuffer
    int width() const { return w_; }
    int height() const { return h_; }
    const GlTexture& color() const { return color_; }

private:
    GlTexture color_;
    void rebuildMultisample();
    void dropMultisample();
    gl::GLuint depth_ = 0, fbo_ = 0;
    gl::GLuint msColor_ = 0, msDepth_ = 0, msFbo_ = 0;
    int samples_ = 0;
    int w_ = 0, h_ = 0;
};

// A depth renderbuffer, to share between render targets of the same size that are never drawn
// into at once (the shadow cubes, drawn one face after another).
class DepthBuffer {
public:
    DepthBuffer() = default;
    ~DepthBuffer() { reset(); }
    DepthBuffer(const DepthBuffer&) = delete;
    DepthBuffer& operator=(const DepthBuffer&) = delete;

    void create(int w, int h);  // replaces any earlier storage
    void reset();
    gl::GLuint id() const { return id_; }

private:
    gl::GLuint id_ = 0;
};

// A cube map of linear distances (one float per texel), drawn with a shared depth buffer: the shadow
// map of one point light. bindFace() makes a face the draw target; texture() is sampled as a
// samplerCube.
class ShadowCube {
public:
    ShadowCube() = default;
    ~ShadowCube() { reset(); }
    ShadowCube(const ShadowCube&) = delete;
    ShadowCube& operator=(const ShadowCube&) = delete;

    void create(int size, const DepthBuffer& depth);
    void reset();
    void bindFace(int face) const;  // 0..5 = +X -X +Y -Y +Z -Z; also sets the viewport
    gl::GLuint texture() const { return tex_; }
    int size() const { return size_; }
    bool valid() const { return fbo_ != 0; }

private:
    gl::GLuint tex_ = 0, fbo_ = 0;
    int size_ = 0;
};

// A linked shader program with named uniform setters (locations are looked up once and cached).
// Call use() before the setters.
class GlProgram {
public:
    using Attrib = std::pair<gl::GLuint, const char*>;

    GlProgram() = default;
    ~GlProgram() { reset(); }
    GlProgram(GlProgram&& o) noexcept : id_(std::exchange(o.id_, 0)), uniforms_(std::move(o.uniforms_)) {}
    GlProgram& operator=(GlProgram&& o) noexcept {
        if (this != &o) {
            reset();
            id_ = std::exchange(o.id_, 0);
            uniforms_ = std::move(o.uniforms_);
        }
        return *this;
    }
    GlProgram(const GlProgram&) = delete;
    GlProgram& operator=(const GlProgram&) = delete;

    // Compiles and links; fragmentExtra (may be null) is appended to the fragment shader.
    // Attribute names are bound to the given locations. Logs errors and returns false on failure.
    bool build(const char* vertexSrc, const char* fragmentSrc, std::initializer_list<Attrib> attribs,
               const char* fragmentExtra = nullptr);
    void reset();
    bool valid() const { return id_ != 0; }
    void use() const;

    gl::GLint uniform(std::string_view name) const;  // -1 when absent
    void set(std::string_view name, int v) const;
    void set(std::string_view name, float v) const;
    void set(std::string_view name, float a, float b) const;
    void set(std::string_view name, float a, float b, float c) const;
    void setMat4(std::string_view name, const float* m) const;
    void setVec3Array(std::string_view name, int count, const float* v) const;
    void setFloatArray(std::string_view name, int count, const float* v) const;

private:
    struct StringHash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };
    gl::GLuint id_ = 0;
    mutable std::unordered_map<std::string, gl::GLint, StringHash, std::equal_to<>> uniforms_;
};

}  // namespace mythos
