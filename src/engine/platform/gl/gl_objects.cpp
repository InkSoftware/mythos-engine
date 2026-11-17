#include "engine/platform/gl/gl_objects.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>

namespace mythos {

using namespace gl;

// ---- GlBuffer
void GlBuffer::create() {
    if (!id_) GenBuffers(1, &id_);
}
void GlBuffer::reset() {
    if (id_ && gl::loaded()) DeleteBuffers(1, &id_);
    id_ = 0;
}
void GlBuffer::upload(const void* data, size_t bytes, gl::GLenum usage) {
    create();
    BindBuffer(ARRAY_BUFFER, id_);
    BufferData(ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), data, usage);
}

// ---- GlVertexArray
void GlVertexArray::create() {
    if (!id_) GenVertexArrays(1, &id_);
}
void GlVertexArray::reset() {
    if (id_ && gl::loaded()) DeleteVertexArrays(1, &id_);
    id_ = 0;
}
void GlVertexArray::bind() const { BindVertexArray(id_); }

// ---- GlTexture
void GlTexture::create() {
    if (!id_) GenTextures(1, &id_);
}
void GlTexture::reset() {
    if (id_ && gl::loaded()) DeleteTextures(1, &id_);
    id_ = 0;
}
void GlTexture::bind() const { BindTexture(TEXTURE_2D, id_); }
void GlTexture::setParams(GLint minFilter, GLint magFilter, GLint wrap) {
    bind();
    TexParameteri(TEXTURE_2D, TEXTURE_MIN_FILTER, minFilter);
    TexParameteri(TEXTURE_2D, TEXTURE_MAG_FILTER, magFilter);
    TexParameteri(TEXTURE_2D, TEXTURE_WRAP_S, wrap);
    TexParameteri(TEXTURE_2D, TEXTURE_WRAP_T, wrap);
}
void GlTexture::allocate(int w, int h, const void* rgbaPixels) {
    bind();
    TexImage2D(TEXTURE_2D, 0, RGBA8, w, h, 0, RGBA, UNSIGNED_BYTE, rgbaPixels);
}

void surfaceToRgbaFlipped(const Surface& s, std::vector<uint8_t>& out) {
    out.resize(static_cast<size_t>(s.w) * static_cast<size_t>(s.h) * 4);
    for (int y = 0; y < s.h; ++y) {
        const uint32_t* src = &s.px[static_cast<size_t>(s.h - 1 - y) * static_cast<size_t>(s.w)];
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

// ---- RenderTarget
void RenderTarget::create(int w, int h) {
    reset();
    w_ = w;
    h_ = h;
    color_.create();
    color_.allocate(w, h);
    color_.setParams(LINEAR, LINEAR, CLAMP_TO_EDGE);
    GenRenderbuffers(1, &depth_);
    BindRenderbuffer(RENDERBUFFER, depth_);
    RenderbufferStorage(RENDERBUFFER, DEPTH_COMPONENT24, w, h);
    GenFramebuffers(1, &fbo_);
    BindFramebuffer(FRAMEBUFFER, fbo_);
    FramebufferTexture2D(FRAMEBUFFER, COLOR_ATTACHMENT0, TEXTURE_2D, color_.id(), 0);
    FramebufferRenderbuffer(FRAMEBUFFER, DEPTH_ATTACHMENT, RENDERBUFFER, depth_);
    BindFramebuffer(FRAMEBUFFER, 0);
}
int RenderTarget::maxSamples() {
    static int cached = -1;
    if (cached < 0 || !gl::loaded()) {
        GLint v = 0;
        if (gl::loaded()) GetIntegerv(MAX_SAMPLES, &v);
        cached = std::max(0, static_cast<int>(v));
    }
    return cached;
}
void RenderTarget::dropMultisample() {
    if (gl::loaded()) {
        if (msFbo_) DeleteFramebuffers(1, &msFbo_);
        if (msColor_) DeleteRenderbuffers(1, &msColor_);
        if (msDepth_) DeleteRenderbuffers(1, &msDepth_);
    }
    msFbo_ = msColor_ = msDepth_ = 0;
}
void RenderTarget::rebuildMultisample() {
    dropMultisample();
    if (samples_ < 2 || w_ <= 0 || h_ <= 0) return;
    GenRenderbuffers(1, &msColor_);
    BindRenderbuffer(RENDERBUFFER, msColor_);
    RenderbufferStorageMultisample(RENDERBUFFER, samples_, RGBA8, w_, h_);
    GenRenderbuffers(1, &msDepth_);
    BindRenderbuffer(RENDERBUFFER, msDepth_);
    RenderbufferStorageMultisample(RENDERBUFFER, samples_, DEPTH_COMPONENT24, w_, h_);
    GenFramebuffers(1, &msFbo_);
    BindFramebuffer(FRAMEBUFFER, msFbo_);
    FramebufferRenderbuffer(FRAMEBUFFER, COLOR_ATTACHMENT0, RENDERBUFFER, msColor_);
    FramebufferRenderbuffer(FRAMEBUFFER, DEPTH_ATTACHMENT, RENDERBUFFER, msDepth_);
    const bool ok = CheckFramebufferStatus(FRAMEBUFFER) == FRAMEBUFFER_COMPLETE;
    BindFramebuffer(FRAMEBUFFER, 0);
    if (!ok) {
        SDL_Log("multisampled framebuffer incomplete: anti-aliasing off");
        dropMultisample();
        samples_ = 0;
    }
}
void RenderTarget::setSamples(int n) {
    n = n < 2 ? 0 : std::min(n, maxSamples());
    if (n < 2) n = 0;
    if (n == samples_) return;
    samples_ = n;
    rebuildMultisample();
}
void RenderTarget::resolve() const {
    if (!msFbo_) return;
    BindFramebuffer(READ_FRAMEBUFFER, msFbo_);
    BindFramebuffer(DRAW_FRAMEBUFFER, fbo_);
    BlitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, COLOR_BUFFER_BIT, NEAREST_FILTER);
    BindFramebuffer(FRAMEBUFFER, 0);
}
void RenderTarget::reset() {
    dropMultisample();
    samples_ = 0;
    if (gl::loaded()) {
        if (fbo_) DeleteFramebuffers(1, &fbo_);
        if (depth_) DeleteRenderbuffers(1, &depth_);
    }
    fbo_ = depth_ = 0;
    color_.reset();
    w_ = h_ = 0;
}
void RenderTarget::resize(int w, int h) {
    if (w == w_ && h == h_) return;
    w_ = w;
    h_ = h;
    color_.allocate(w, h);
    BindRenderbuffer(RENDERBUFFER, depth_);
    RenderbufferStorage(RENDERBUFFER, DEPTH_COMPONENT24, w, h);
    rebuildMultisample();
}
void RenderTarget::bind() const { BindFramebuffer(FRAMEBUFFER, msFbo_ ? msFbo_ : fbo_); }
void RenderTarget::bindWindow() { BindFramebuffer(FRAMEBUFFER, 0); }

// ---- DepthBuffer
void DepthBuffer::create(int w, int h) {
    if (!id_) GenRenderbuffers(1, &id_);
    BindRenderbuffer(RENDERBUFFER, id_);
    RenderbufferStorage(RENDERBUFFER, DEPTH_COMPONENT24, w, h);
}
void DepthBuffer::reset() {
    if (id_ && gl::loaded()) DeleteRenderbuffers(1, &id_);
    id_ = 0;
}

// ---- ShadowCube
void ShadowCube::create(int size, const DepthBuffer& depth) {
    reset();
    size_ = size;
    GenTextures(1, &tex_);
    BindTexture(TEXTURE_CUBE_MAP, tex_);
    for (int f = 0; f < 6; ++f)
        TexImage2D(TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(f), 0, R32F, size, size, 0, RED, FLOAT, nullptr);
    TexParameteri(TEXTURE_CUBE_MAP, TEXTURE_MIN_FILTER, NEAREST);
    TexParameteri(TEXTURE_CUBE_MAP, TEXTURE_MAG_FILTER, NEAREST);
    TexParameteri(TEXTURE_CUBE_MAP, TEXTURE_WRAP_S, CLAMP_TO_EDGE);
    TexParameteri(TEXTURE_CUBE_MAP, TEXTURE_WRAP_T, CLAMP_TO_EDGE);
    GenFramebuffers(1, &fbo_);
    BindFramebuffer(FRAMEBUFFER, fbo_);
    FramebufferRenderbuffer(FRAMEBUFFER, DEPTH_ATTACHMENT, RENDERBUFFER, depth.id());
    FramebufferTexture2D(FRAMEBUFFER, COLOR_ATTACHMENT0, TEXTURE_CUBE_MAP_POSITIVE_X, tex_, 0);
    if (CheckFramebufferStatus(FRAMEBUFFER) != FRAMEBUFFER_COMPLETE) {
        SDL_Log("shadow cube framebuffer incomplete: shadows off");
        BindFramebuffer(FRAMEBUFFER, 0);
        reset();
        return;
    }
    BindFramebuffer(FRAMEBUFFER, 0);
}
void ShadowCube::reset() {
    if (gl::loaded()) {
        if (fbo_) DeleteFramebuffers(1, &fbo_);
        if (tex_) DeleteTextures(1, &tex_);
    }
    tex_ = fbo_ = 0;
    size_ = 0;
}
void ShadowCube::bindFace(int face) const {
    BindFramebuffer(FRAMEBUFFER, fbo_);
    FramebufferTexture2D(FRAMEBUFFER, COLOR_ATTACHMENT0, TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(face), tex_, 0);
    Viewport(0, 0, size_, size_);
}

// ---- GlProgram
bool GlProgram::build(const char* vertexSrc, const char* fragmentSrc, std::initializer_list<Attrib> attribs,
                      const char* fragmentExtra) {
    reset();
    auto compile = [](GLenum type, const char* src, const char* extra) -> GLuint {
        GLuint s = CreateShader(type);
        const char* sources[2] = {src, extra};
        ShaderSource(s, extra ? 2 : 1, sources, nullptr);
        CompileShader(s);
        GLint ok = 0;
        GetShaderiv(s, COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024] = {0};
            GetShaderInfoLog(s, sizeof(log), nullptr, log);
            SDL_Log("shader compile error: %s", log);
        }
        return s;
    };
    GLuint vs = compile(VERTEX_SHADER, vertexSrc, nullptr);
    GLuint fs = compile(FRAGMENT_SHADER, fragmentSrc, fragmentExtra);
    GLuint p = CreateProgram();
    AttachShader(p, vs);
    AttachShader(p, fs);
    for (const Attrib& a : attribs) BindAttribLocation(p, a.first, a.second);  // unused names are ignored
    LinkProgram(p);
    GLint ok = 0;
    GetProgramiv(p, LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        GetProgramInfoLog(p, sizeof(log), nullptr, log);
        SDL_Log("program link error: %s", log);
    }
    DeleteShader(vs);
    DeleteShader(fs);
    if (!ok) {
        DeleteProgram(p);
        return false;
    }
    id_ = p;
    return true;
}
void GlProgram::reset() {
    if (id_ && gl::loaded()) DeleteProgram(id_);
    id_ = 0;
    uniforms_.clear();
}
void GlProgram::use() const { UseProgram(id_); }

GLint GlProgram::uniform(std::string_view name) const {
    auto it = uniforms_.find(name);
    if (it != uniforms_.end()) return it->second;
    const GLint loc = GetUniformLocation(id_, std::string(name).c_str());
    uniforms_.emplace(std::string(name), loc);
    return loc;
}
void GlProgram::set(std::string_view n, int v) const { Uniform1i(uniform(n), v); }
void GlProgram::set(std::string_view n, float v) const { Uniform1f(uniform(n), v); }
void GlProgram::set(std::string_view n, float a, float b) const { Uniform2f(uniform(n), a, b); }
void GlProgram::set(std::string_view n, float a, float b, float c) const { Uniform3f(uniform(n), a, b, c); }
void GlProgram::setMat4(std::string_view n, const float* m) const { UniformMatrix4fv(uniform(n), 1, FALSE_, m); }
void GlProgram::setVec3Array(std::string_view n, int count, const float* v) const { Uniform3fv(uniform(n), count, v); }
void GlProgram::setFloatArray(std::string_view n, int count, const float* v) const { Uniform1fv(uniform(n), count, v); }

}  // namespace mythos
