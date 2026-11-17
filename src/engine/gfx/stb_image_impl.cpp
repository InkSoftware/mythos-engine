// The single translation unit that compiles stb_image (public domain, third_party/stb).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO  // everything is read through mythos::vfs and decoded from memory
#include "stb_image.h"
