#ifndef MYTHOS_ENGINE_GFX_TEXTURES_H_
#define MYTHOS_ENGINE_GFX_TEXTURES_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "engine/gfx/surface.h"

// Named textures. A game registers its textures (procedural art generated in
// code) with Add(); PNG, BMP, TGA or JPG files in an asset folder can then
// override any registered name. Art can be flagged as a placeholder until a
// real file replaces it.
namespace mythos {
// A registry of named surfaces. Lookups of unknown names yield a magenta and
// black "missing" texture rather than failing.
class TextureLibrary {
 public:
  // Creates a library holding only the "missing" texture.
  TextureLibrary();

  // Registers or replaces a texture under `name`. `placeholder` marks it as
  // stand-in art until an override file replaces it.
  void Add(const std::string& name, Surface surface, bool placeholder = false);

  // Replaces registered textures with `<dir>/<name>.png` (or .bmp, .tga, .jpg,
  // tried in that order) where such a file exists and decodes; larger images
  // are resampled down to 1024x1024. Only registered names are looked for.
  // Returns the number replaced.
  int LoadOverrides(const std::string& dir);

  // The named texture, or the "missing" texture.
  [[nodiscard]] const Surface& Get(const std::string& name) const;

  [[nodiscard]] bool Has(const std::string& name) const {
    return textures_.contains(name);
  }

  // True if the texture is still placeholder art.
  [[nodiscard]] bool IsPlaceholder(const std::string& name) const {
    return placeholders_.contains(name);
  }

  // Registered names in registration order ("missing" excluded).
  [[nodiscard]] const std::vector<std::string>& Names() const { return names_; }

 private:
  std::map<std::string, Surface> textures_;
  std::vector<std::string> names_;
  std::set<std::string> placeholders_;
};

// Decodes the bytes of an image (PNG, BMP, TGA, JPG...) into `out`; false on
// any problem.
bool DecodeImage(const std::string& bytes, Surface& out);

// Decodes an image file (stb_image: PNG, BMP, TGA, JPG and more) read through
// the vfs into `out`. Returns false on any problem.
bool LoadImage(const std::string& path, Surface& out);

// Nearest-neighbor resample to w by h. An empty `src` gives a blank
// (transparent black) surface of that size.
Surface Resample(const Surface& src, int w, int h);
}

#endif
