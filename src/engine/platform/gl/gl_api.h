#ifndef MYTHOS_ENGINE_PLATFORM_GL_GL_API_H_
#define MYTHOS_ENGINE_PLATFORM_GL_GL_API_H_
// Minimal OpenGL 4.1 core-profile function loader. Entry points are fetched
// with SDL_GL_GetProcAddress, so no GL headers or extra libraries are needed on
// macOS, Linux or Windows. Only the calls the game uses are declared.

#include <cstddef>

#ifdef _WIN32
#define MYTHOS_GL_CALL __stdcall
#else
#define MYTHOS_GL_CALL
#endif

namespace gl {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLfloat = float;
using GLchar = char;
using GLbitfield = unsigned int;
using GLsizeiptr = std::ptrdiff_t;

constexpr GLboolean kFalse = 0;
constexpr GLenum kFloat = 0x1406;
constexpr GLenum kUnsignedByte = 0x1401;
constexpr GLenum kRgba = 0x1908;
constexpr GLenum kBgra = 0x80E1;
constexpr GLenum kUnsignedInt8888Rev = 0x8367;
constexpr GLenum kRgba8 = 0x8058;
constexpr GLenum kSrgb8Alpha8 = 0x8C43;
constexpr GLenum kRed = 0x1903;
constexpr GLenum kR8 = 0x8229;
constexpr GLenum kUnpackAlignment = 0x0CF5;
constexpr GLenum kDepthComponent24 = 0x81A6;
constexpr GLenum kTriangles = 0x0004;
constexpr GLbitfield kColorBufferBit = 0x00004000;
constexpr GLbitfield kDepthBufferBit = 0x00000100;
constexpr GLenum kDepthTest = 0x0B71;
constexpr GLenum kBlend = 0x0BE2;
constexpr GLenum kLequal = 0x0203;
constexpr GLenum kSrcAlpha = 0x0302;
constexpr GLenum kOneMinusSrcAlpha = 0x0303;
constexpr GLenum kArrayBuffer = 0x8892;
constexpr GLenum kStaticDraw = 0x88E4;
constexpr GLenum kDynamicDraw = 0x88E8;
constexpr GLenum kVertexShader = 0x8B31;
constexpr GLenum kFragmentShader = 0x8B30;
constexpr GLenum kCompileStatus = 0x8B81;
constexpr GLenum kLinkStatus = 0x8B82;
constexpr GLenum kTexture2D = 0x0DE1;
constexpr GLenum kTextureMinFilter = 0x2801;
constexpr GLenum kTextureMagFilter = 0x2800;
constexpr GLenum kTextureWrapS = 0x2802;
constexpr GLenum kTextureWrapT = 0x2803;
constexpr GLint kLinear = 0x2601;
constexpr GLint kLinearMipmapLinear = 0x2703;
constexpr GLint kNearest = 0x2600;
constexpr GLint kRepeat = 0x2901;
constexpr GLint kClampToEdge = 0x812F;
constexpr GLenum kTexture0 = 0x84C0;
constexpr GLenum kTextureCubeMap = 0x8513;
constexpr GLenum kTextureCubeMapPositiveX = 0x8515;
constexpr GLenum kR32F = 0x822E;
constexpr GLenum kFramebuffer = 0x8D40;
constexpr GLenum kColorAttachment0 = 0x8CE0;
constexpr GLenum kDepthAttachment = 0x8D00;
constexpr GLenum kRenderbuffer = 0x8D41;
constexpr GLenum kFramebufferComplete = 0x8CD5;
constexpr GLenum kReadFramebuffer = 0x8CA8;
constexpr GLenum kDrawFramebuffer = 0x8CA9;
constexpr GLenum kMaxSamples = 0x8D57;
constexpr GLenum kNearestFilter =
    0x2600;  // the filter argument of BlitFramebuffer

// X(return type, name, parameters...)
#define MYTHOS_GL_FUNCTIONS(X)                                               \
  X(void, Viewport, GLint x, GLint y, GLsizei w, GLsizei h)                  \
  X(void, ClearColor, GLfloat r, GLfloat g, GLfloat b, GLfloat a)            \
  X(void, Clear, GLbitfield mask)                                            \
  X(void, Enable, GLenum cap)                                                \
  X(void, Disable, GLenum cap)                                               \
  X(void, BlendFunc, GLenum src, GLenum dst)                                 \
  X(void, DepthFunc, GLenum func)                                            \
  X(void, DrawArrays, GLenum mode, GLint first, GLsizei count)               \
  X(void, GenTextures, GLsizei n, GLuint* textures)                          \
  X(void, DeleteTextures, GLsizei n, const GLuint* textures)                 \
  X(void, TexSubImage2D, GLenum target, GLint level, GLint x, GLint y,       \
    GLsizei w, GLsizei h, GLenum format, GLenum type, const void* data)      \
  X(void, DeleteVertexArrays, GLsizei n, const GLuint* arrays)               \
  X(void, DeleteProgram, GLuint program)                                     \
  X(void, DeleteFramebuffers, GLsizei n, const GLuint* framebuffers)         \
  X(void, DeleteRenderbuffers, GLsizei n, const GLuint* renderbuffers)       \
  X(void, BindTexture, GLenum target, GLuint texture)                        \
  X(void, TexImage2D, GLenum target, GLint level, GLint internal, GLsizei w, \
    GLsizei h, GLint border, GLenum format, GLenum type, const void* data)   \
  X(void, TexParameteri, GLenum target, GLenum pname, GLint param)           \
  X(void, PixelStorei, GLenum pname, GLint param)                            \
  X(void, GenerateMipmap, GLenum target)                                     \
  X(void, ReadPixels, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, \
    GLenum type, void* data)                                                 \
  X(void, ActiveTexture, GLenum texture)                                     \
  X(void, GenVertexArrays, GLsizei n, GLuint* arrays)                        \
  X(void, BindVertexArray, GLuint array)                                     \
  X(void, GenBuffers, GLsizei n, GLuint* buffers)                            \
  X(void, DeleteBuffers, GLsizei n, const GLuint* buffers)                   \
  X(void, BindBuffer, GLenum target, GLuint buffer)                          \
  X(void, BufferData, GLenum target, GLsizeiptr size, const void* data,      \
    GLenum usage)                                                            \
  X(void, EnableVertexAttribArray, GLuint index)                             \
  X(void, DisableVertexAttribArray, GLuint index)                            \
  X(void, VertexAttribPointer, GLuint index, GLint size, GLenum type,        \
    GLboolean normalized, GLsizei stride, const void* pointer)               \
  X(void, BindAttribLocation, GLuint program, GLuint index,                  \
    const GLchar* name)                                                      \
  X(GLuint, CreateShader, GLenum type)                                       \
  X(void, ShaderSource, GLuint shader, GLsizei count,                        \
    const GLchar* const* source, const GLint* length)                        \
  X(void, CompileShader, GLuint shader)                                      \
  X(void, GetShaderiv, GLuint shader, GLenum pname, GLint* params)           \
  X(void, GetShaderInfoLog, GLuint shader, GLsizei max_length,               \
    GLsizei* length, GLchar* log)                                            \
  X(void, DeleteShader, GLuint shader)                                       \
  X(GLuint, CreateProgram, void)                                             \
  X(void, AttachShader, GLuint program, GLuint shader)                       \
  X(void, LinkProgram, GLuint program)                                       \
  X(void, GetProgramiv, GLuint program, GLenum pname, GLint* params)         \
  X(void, GetProgramInfoLog, GLuint program, GLsizei max_length,             \
    GLsizei* length, GLchar* log)                                            \
  X(void, UseProgram, GLuint program)                                        \
  X(GLint, GetUniformLocation, GLuint program, const GLchar* name)           \
  X(void, Uniform1i, GLint location, GLint v)                                \
  X(void, Uniform1f, GLint location, GLfloat v)                              \
  X(void, Uniform2f, GLint location, GLfloat a, GLfloat b)                   \
  X(void, Uniform4f, GLint location, GLfloat a, GLfloat b, GLfloat c,        \
    GLfloat d)                                                               \
  X(void, Uniform3f, GLint location, GLfloat a, GLfloat b, GLfloat c)        \
  X(void, Uniform1fv, GLint location, GLsizei count, const GLfloat* value)   \
  X(void, Uniform3fv, GLint location, GLsizei count, const GLfloat* value)   \
  X(void, Uniform2fv, GLint location, GLsizei count, const GLfloat* value)   \
  X(void, Uniform4fv, GLint location, GLsizei count, const GLfloat* value)   \
  X(void, UniformMatrix4fv, GLint location, GLsizei count,                   \
    GLboolean transpose, const GLfloat* value)                               \
  X(void, GenFramebuffers, GLsizei n, GLuint* framebuffers)                  \
  X(void, BindFramebuffer, GLenum target, GLuint framebuffer)                \
  X(void, FramebufferTexture2D, GLenum target, GLenum attachment,            \
    GLenum textarget, GLuint texture, GLint level)                           \
  X(void, RenderbufferStorageMultisample, GLenum target, GLsizei samples,    \
    GLenum internalformat, GLsizei w, GLsizei h)                             \
  X(void, BlitFramebuffer, GLint sx0, GLint sy0, GLint sx1, GLint sy1,       \
    GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask,             \
    GLenum filter)                                                           \
  X(void, GetIntegerv, GLenum pname, GLint* data)                            \
  X(GLenum, CheckFramebufferStatus, GLenum target)                           \
  X(void, GenRenderbuffers, GLsizei n, GLuint* renderbuffers)                \
  X(void, BindRenderbuffer, GLenum target, GLuint renderbuffer)              \
  X(void, RenderbufferStorage, GLenum target, GLenum internalformat,         \
    GLsizei w, GLsizei h)                                                    \
  X(void, FramebufferRenderbuffer, GLenum target, GLenum attachment,         \
    GLenum renderbuffertarget, GLuint renderbuffer)

#define MYTHOS_GL_DECLARE(ret, name, ...) \
  extern ret(MYTHOS_GL_CALL* name)(__VA_ARGS__);
MYTHOS_GL_FUNCTIONS(MYTHOS_GL_DECLARE)
#undef MYTHOS_GL_DECLARE

// Loads every function above. Must be called after the GL context is current.
bool LoadFunctions();
// Forgets every function (call when the context is destroyed). RAII wrappers
// check for this, so they can safely outlive the context without calling into a
// dead driver.
void UnloadFunctions();
// True while the functions are loaded, i.e. a context exists.
bool Loaded();

}  // namespace gl

#endif  // MYTHOS_ENGINE_PLATFORM_GL_GL_API_H_
