#pragma once
// Minimal OpenGL 4.1 core-profile function loader. Entry points are fetched with
// SDL_GL_GetProcAddress, so no GL headers or extra libraries are needed on macOS,
// Linux or Windows. Only the calls the game uses are declared.

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

constexpr GLboolean FALSE_ = 0;
constexpr GLenum FLOAT = 0x1406;
constexpr GLenum UNSIGNED_BYTE = 0x1401;
constexpr GLenum RGBA = 0x1908;
constexpr GLenum BGRA = 0x80E1;
constexpr GLenum UNSIGNED_INT_8_8_8_8_REV = 0x8367;
constexpr GLenum RGBA8 = 0x8058;
constexpr GLenum RED = 0x1903;
constexpr GLenum R8 = 0x8229;
constexpr GLenum UNPACK_ALIGNMENT = 0x0CF5;
constexpr GLenum DEPTH_COMPONENT24 = 0x81A6;
constexpr GLenum TRIANGLES = 0x0004;
constexpr GLbitfield COLOR_BUFFER_BIT = 0x00004000;
constexpr GLbitfield DEPTH_BUFFER_BIT = 0x00000100;
constexpr GLenum DEPTH_TEST = 0x0B71;
constexpr GLenum BLEND = 0x0BE2;
constexpr GLenum LEQUAL = 0x0203;
constexpr GLenum SRC_ALPHA = 0x0302;
constexpr GLenum ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum ARRAY_BUFFER = 0x8892;
constexpr GLenum STATIC_DRAW = 0x88E4;
constexpr GLenum DYNAMIC_DRAW = 0x88E8;
constexpr GLenum VERTEX_SHADER = 0x8B31;
constexpr GLenum FRAGMENT_SHADER = 0x8B30;
constexpr GLenum COMPILE_STATUS = 0x8B81;
constexpr GLenum LINK_STATUS = 0x8B82;
constexpr GLenum TEXTURE_2D = 0x0DE1;
constexpr GLenum TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum TEXTURE_WRAP_S = 0x2802;
constexpr GLenum TEXTURE_WRAP_T = 0x2803;
constexpr GLint LINEAR = 0x2601;
constexpr GLint LINEAR_MIPMAP_LINEAR = 0x2703;
constexpr GLint NEAREST = 0x2600;
constexpr GLint REPEAT = 0x2901;
constexpr GLint CLAMP_TO_EDGE = 0x812F;
constexpr GLenum TEXTURE0 = 0x84C0;
constexpr GLenum TEXTURE5 = 0x84C5;
constexpr GLenum TEXTURE_CUBE_MAP = 0x8513;
constexpr GLenum TEXTURE_CUBE_MAP_POSITIVE_X = 0x8515;
constexpr GLenum R32F = 0x822E;
constexpr GLenum FRAMEBUFFER = 0x8D40;
constexpr GLenum COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum DEPTH_ATTACHMENT = 0x8D00;
constexpr GLenum RENDERBUFFER = 0x8D41;
constexpr GLenum FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum READ_FRAMEBUFFER = 0x8CA8;
constexpr GLenum DRAW_FRAMEBUFFER = 0x8CA9;
constexpr GLenum MAX_SAMPLES = 0x8D57;
constexpr GLenum NEAREST_FILTER = 0x2600;  // the filter argument of BlitFramebuffer

// X(return type, name, parameters...)
#define MYTHOS_GL_FUNCTIONS(X)                                                                                  \
    X(void, Viewport, GLint x, GLint y, GLsizei w, GLsizei h)                                               \
    X(void, ClearColor, GLfloat r, GLfloat g, GLfloat b, GLfloat a)                                         \
    X(void, Clear, GLbitfield mask)                                                                         \
    X(void, Enable, GLenum cap)                                                                             \
    X(void, Disable, GLenum cap)                                                                            \
    X(void, BlendFunc, GLenum src, GLenum dst)                                                              \
    X(void, DepthFunc, GLenum func)                                                                         \
    X(void, DrawArrays, GLenum mode, GLint first, GLsizei count)                                            \
    X(void, GenTextures, GLsizei n, GLuint* textures)                                                       \
    X(void, DeleteTextures, GLsizei n, const GLuint* textures)                                              \
    X(void, TexSubImage2D, GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, \
      GLenum type, const void* data)                                                                        \
    X(void, DeleteVertexArrays, GLsizei n, const GLuint* arrays)                                            \
    X(void, DeleteProgram, GLuint program)                                                                  \
    X(void, DeleteFramebuffers, GLsizei n, const GLuint* framebuffers)                                      \
    X(void, DeleteRenderbuffers, GLsizei n, const GLuint* renderbuffers)                                    \
    X(void, BindTexture, GLenum target, GLuint texture)                                                     \
    X(void, TexImage2D, GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border,     \
      GLenum format, GLenum type, const void* data)                                                         \
    X(void, TexParameteri, GLenum target, GLenum pname, GLint param)                                        \
    X(void, PixelStorei, GLenum pname, GLint param)                                                         \
    X(void, GenerateMipmap, GLenum target)                                                                  \
    X(void, ReadPixels, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data)     \
    X(void, ActiveTexture, GLenum texture)                                                                  \
    X(void, GenVertexArrays, GLsizei n, GLuint* arrays)                                                     \
    X(void, BindVertexArray, GLuint array)                                                                  \
    X(void, GenBuffers, GLsizei n, GLuint* buffers)                                                         \
    X(void, DeleteBuffers, GLsizei n, const GLuint* buffers)                                                \
    X(void, BindBuffer, GLenum target, GLuint buffer)                                                       \
    X(void, BufferData, GLenum target, GLsizeiptr size, const void* data, GLenum usage)                     \
    X(void, EnableVertexAttribArray, GLuint index)                                                          \
    X(void, DisableVertexAttribArray, GLuint index)                                                         \
    X(void, VertexAttribPointer, GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, \
      const void* pointer)                                                                                  \
    X(void, BindAttribLocation, GLuint program, GLuint index, const GLchar* name)                           \
    X(GLuint, CreateShader, GLenum type)                                                                    \
    X(void, ShaderSource, GLuint shader, GLsizei count, const GLchar* const* source, const GLint* length)   \
    X(void, CompileShader, GLuint shader)                                                                   \
    X(void, GetShaderiv, GLuint shader, GLenum pname, GLint* params)                                        \
    X(void, GetShaderInfoLog, GLuint shader, GLsizei maxLength, GLsizei* length, GLchar* log)               \
    X(void, DeleteShader, GLuint shader)                                                                    \
    X(GLuint, CreateProgram, void)                                                                          \
    X(void, AttachShader, GLuint program, GLuint shader)                                                    \
    X(void, LinkProgram, GLuint program)                                                                    \
    X(void, GetProgramiv, GLuint program, GLenum pname, GLint* params)                                      \
    X(void, GetProgramInfoLog, GLuint program, GLsizei maxLength, GLsizei* length, GLchar* log)             \
    X(void, UseProgram, GLuint program)                                                                     \
    X(GLint, GetUniformLocation, GLuint program, const GLchar* name)                                        \
    X(void, Uniform1i, GLint location, GLint v)                                                             \
    X(void, Uniform1f, GLint location, GLfloat v)                                                           \
    X(void, Uniform2f, GLint location, GLfloat a, GLfloat b)                                                \
    X(void, Uniform3f, GLint location, GLfloat a, GLfloat b, GLfloat c)                                     \
    X(void, Uniform1fv, GLint location, GLsizei count, const GLfloat* value)                                \
    X(void, Uniform3fv, GLint location, GLsizei count, const GLfloat* value)                                \
    X(void, UniformMatrix4fv, GLint location, GLsizei count, GLboolean transpose, const GLfloat* value)     \
    X(void, GenFramebuffers, GLsizei n, GLuint* framebuffers)                                               \
    X(void, BindFramebuffer, GLenum target, GLuint framebuffer)                                             \
    X(void, FramebufferTexture2D, GLenum target, GLenum attachment, GLenum textarget, GLuint texture,       \
      GLint level)                                                                                          \
    X(void, RenderbufferStorageMultisample, GLenum target, GLsizei samples, GLenum internalformat, GLsizei w,  \
      GLsizei h)                                                                                            \
    X(void, BlitFramebuffer, GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1,   \
      GLint dy1, GLbitfield mask, GLenum filter)                                                            \
    X(void, GetIntegerv, GLenum pname, GLint* data)                                                         \
    X(GLenum, CheckFramebufferStatus, GLenum target)                                                        \
    X(void, GenRenderbuffers, GLsizei n, GLuint* renderbuffers)                                             \
    X(void, BindRenderbuffer, GLenum target, GLuint renderbuffer)                                           \
    X(void, RenderbufferStorage, GLenum target, GLenum internalformat, GLsizei w, GLsizei h)                \
    X(void, FramebufferRenderbuffer, GLenum target, GLenum attachment, GLenum renderbuffertarget,           \
      GLuint renderbuffer)

#define MYTHOS_GL_DECLARE(ret, name, ...) extern ret(MYTHOS_GL_CALL* name)(__VA_ARGS__);
MYTHOS_GL_FUNCTIONS(MYTHOS_GL_DECLARE)
#undef MYTHOS_GL_DECLARE

// Loads every function above. Must be called after the GL context is current.
bool loadFunctions();
// Forgets every function (call when the context is destroyed). RAII wrappers check for this, so
// they can safely outlive the context without calling into a dead driver.
void unloadFunctions();
// True while the functions are loaded, i.e. a context exists.
bool loaded();

}  // namespace gl
