#include "engine/gfx/textures.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "engine/core/vfs.h"
#include "stb_image.h"

namespace mythos {
namespace {

Surface makeMissing() {
    Surface s(16, 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) s.at(x, y) = ((x / 4 + y / 4) % 2) ? rgba(255, 0, 255) : rgba(0, 0, 0);
    return s;
}

}  // namespace

TextureLibrary::TextureLibrary() { textures_["missing"] = makeMissing(); }

void TextureLibrary::add(const std::string& name, Surface surface, bool placeholder) {
    if (textures_.count(name) == 0) names_.push_back(name);
    textures_[name] = std::move(surface);
    if (placeholder) placeholders_.insert(name);
    else placeholders_.erase(name);
}

int TextureLibrary::loadOverrides(const std::string& dir) {
    int replaced = 0;
    for (const auto& name : names_) {
        Surface loaded;
        bool found = false;
        for (const char* ext : {".png", ".bmp", ".tga", ".jpg"}) {  // first format that exists wins
            const std::string path = dir + "/" + name + ext;
            if (vfs::exists(path) && loadImage(path, loaded)) {
                found = true;
                break;
            }
        }
        if (!found) continue;
        // Overrides keep their own resolution (capped so a stray huge file cannot eat memory).
        constexpr int kMaxSide = 1024;
        if (loaded.w > kMaxSide || loaded.h > kMaxSide) loaded = resample(loaded, kMaxSide, kMaxSide);
        textures_[name] = std::move(loaded);
        placeholders_.erase(name);
        ++replaced;
    }
    return replaced;
}

const Surface& TextureLibrary::get(const std::string& name) const {
    auto it = textures_.find(name);
    return it != textures_.end() ? it->second : textures_.at("missing");
}

bool loadImage(const std::string& path, Surface& out) {
    std::string bytes;
    if (!vfs::readFile(path, bytes) || bytes.empty()) return false;
    int w = 0, h = 0, channels = 0;
    unsigned char* data = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()),
                                                static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (!data) return false;
    out = Surface(w, h);
    for (size_t i = 0; i < out.px.size(); ++i) out.px[i] = rgba(data[i * 4], data[i * 4 + 1], data[i * 4 + 2], data[i * 4 + 3]);
    stbi_image_free(data);
    return true;
}

Surface resample(const Surface& src, int w, int h) {
    Surface out(w, h);
    if (src.empty()) return out;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            out.at(x, y) = src.at(x * src.w / w, y * src.h / h);
    return out;
}

}  // namespace mythos
