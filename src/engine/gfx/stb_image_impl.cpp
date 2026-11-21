

// The one translation unit that compiles stb_image. Images are decoded from
// memory (read through the vfs), so the stdio loaders are left out.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO

#include "stb_image.h"
