#include "engine/gfx/textures.h"

#include <vector>

#include "engine/core/vfs.h"
#include "stb_image.h"

namespace mythos {
namespace {

// The magenta and black checkerboard that stands in for any texture name that
// is not registered.
Surface MakeMissing() {
  constexpr int kSize = 16, kSquare = 4;
  Surface missing(kSize, kSize);
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      missing.At(x, y) =
          ((x / kSquare + y / kSquare) % 2) ? Rgba(255, 0, 255) : Rgba(0, 0, 0);
    }
  }
  return missing;
}

}  // namespace

TextureLibrary::TextureLibrary() { textures_["missing"] = MakeMissing(); }

void TextureLibrary::Add(const std::string& name, Surface surface,
                         bool placeholder) {
  if (!textures_.contains(name)) names_.push_back(name);
  textures_[name] = std::move(surface);
  if (placeholder) {
    placeholders_.insert(name);
  } else {
    placeholders_.erase(name);
  }
}

int TextureLibrary::LoadOverrides(const std::string& dir) {
  int replaced = 0;
  for (const auto& name : names_) {
    Surface loaded;
    bool found = false;
    for (const char* ext :
         {".png", ".bmp", ".tga", ".jpg"}) {  // first format that exists wins
      const std::string path = dir + "/" + name + ext;
      if (vfs::Exists(path) && LoadImage(path, loaded)) {
        found = true;
        break;
      }
    }
    if (!found) continue;
    // Overrides keep their own resolution (capped so a stray huge file cannot
    // eat memory).
    constexpr int kMaxSide = 1024;
    if (loaded.w > kMaxSide || loaded.h > kMaxSide) {
      loaded = Resample(loaded, kMaxSide, kMaxSide);
    }
    textures_[name] = std::move(loaded);
    placeholders_.erase(name);
    ++replaced;
  }
  return replaced;
}

const Surface& TextureLibrary::Get(const std::string& name) const {
  const auto it = textures_.find(name);
  return it != textures_.end() ? it->second : textures_.at("missing");
}

bool LoadImage(const std::string& path, Surface& out) {
  std::string bytes;
  if (!vfs::ReadFile(path, bytes) || bytes.empty()) return false;
  int width = 0, height = 0, channels = 0;
  unsigned char* data = stbi_load_from_memory(
      reinterpret_cast<const unsigned char*>(bytes.data()),
      static_cast<int>(bytes.size()), &width, &height, &channels, 4);
  if (!data) return false;
  out = Surface(width, height);
  for (size_t i = 0; i < out.px.size(); ++i) {
    out.px[i] =
        Rgba(data[i * 4], data[i * 4 + 1], data[i * 4 + 2], data[i * 4 + 3]);
  }
  stbi_image_free(data);
  return true;
}

Surface Resample(const Surface& src, int width, int height) {
  Surface out(width, height);
  if (src.Empty()) return out;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      out.At(x, y) = src.At(x * src.w / width, y * src.h / height);
    }
  }
  return out;
}

}  // namespace mythos
