#include "engine/platform/gl/gl_api.h"

#include <SDL3/SDL.h>

namespace gl {

#define MYTHOS_GL_DEFINE(ret, name, ...) ret(MYTHOS_GL_CALL* name)(__VA_ARGS__) = nullptr;
MYTHOS_GL_FUNCTIONS(MYTHOS_GL_DEFINE)
#undef MYTHOS_GL_DEFINE

bool loadFunctions() {
    bool ok = true;
#define MYTHOS_GL_LOAD(ret, name, ...)                                                       \
    name = reinterpret_cast<decltype(name)>(SDL_GL_GetProcAddress("gl" #name));          \
    if (!name) {                                                                         \
        SDL_Log("missing OpenGL function gl%s", #name);                                  \
        ok = false;                                                                      \
    }
    MYTHOS_GL_FUNCTIONS(MYTHOS_GL_LOAD)
#undef MYTHOS_GL_LOAD
    return ok;
}

void unloadFunctions() {
#define MYTHOS_GL_UNLOAD(ret, name, ...) name = nullptr;
    MYTHOS_GL_FUNCTIONS(MYTHOS_GL_UNLOAD)
#undef MYTHOS_GL_UNLOAD
}

bool loaded() { return DeleteBuffers != nullptr; }

}  // namespace gl
