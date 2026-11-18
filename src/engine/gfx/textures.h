#ifndef MYTHOS_ENGINE_GFX_TEXTURES_H_
#define MYTHOS_ENGINE_GFX_TEXTURES_H_
// Named textures. A game registers its textures (procedural art, generated in
// code) with add(); PNG, BMP, TGA or JPG files in an asset folder can then
// override any registered name. Art can be flagged as a PLACEHOLDER until a
// real file replaces it.

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
  void Add(const std::string& name, Surface surface, bool placeholder = false);

  // Replaces registered textures with <dir>/<name>.png (or .bmp, .tga, .jpg)
  // when such a file exists. Returns number replaced.
  int LoadOverrides(const std::string& dir);

  // Returns the named texture, or a magenta "missing" texture.
  const Surface& Get(const std::string& name) const;
  bool Has(const std::string& name) const { return textures_.contains(name); }
  // True if the texture is still placeholder art.
  bool IsPlaceholder(const std::string& name) const {
    return placeholders_.contains(name);
  }

  // Registered names, in registration order ("missing" excluded).
  const std::vector<std::string>& Names() const { return names_; }

 private:
  std::map<std::string, Surface> textures_;
  std::vector<std::string> names_;
  std::set<std::string> placeholders_;
};

// Loads an image (stb_image: PNG, BMP, TGA, JPG, ...) through the vfs. False on
// any problem.
bool LoadImage(const std::string& path, Surface& out);
// Nearest-neighbour resample.
Surface Resample(const Surface& src, int w, int h);

}  // namespace mythos

#endif  // MYTHOS_ENGINE_GFX_TEXTURES_H_
