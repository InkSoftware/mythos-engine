#pragma once
// Named textures. A game registers its textures (procedural art, generated in code) with
// add(); BMP files in an asset folder can then override any registered name. Art can be
// flagged as a PLACEHOLDER until a real file replaces it.

#include <map>
#include <set>
#include <string>
#include <vector>

#include "engine/gfx/surface.h"

namespace mythos {

class TextureLibrary {
public:
    TextureLibrary();  // holds only the magenta "missing" texture

    // Registers (or replaces) a texture under name.
    void add(const std::string& name, Surface surface, bool placeholder = false);

    // Replaces registered textures with <dir>/<name>.bmp when such a file exists.
    // Only uncompressed 24/32-bit BMP is supported. Returns number replaced.
    int loadOverrides(const std::string& dir);

    // Returns the named texture, or a magenta "missing" texture.
    const Surface& get(const std::string& name) const;
    bool has(const std::string& name) const { return textures_.count(name) != 0; }
    // True if the texture is still placeholder art.
    bool isPlaceholder(const std::string& name) const { return placeholders_.count(name) != 0; }

    // Registered names, in registration order ("missing" excluded).
    const std::vector<std::string>& names() const { return names_; }

private:
    std::map<std::string, Surface> textures_;
    std::vector<std::string> names_;
    std::set<std::string> placeholders_;
};

// Loads an uncompressed 24/32-bit BMP. Returns false on any problem.
bool loadBmp(const std::string& path, Surface& out);
// Nearest-neighbour resample.
Surface resample(const Surface& src, int w, int h);

}  // namespace mythos
