#ifndef MYTHOS_ENGINE_GFX_TEXT_H_
#define MYTHOS_ENGINE_GFX_TEXT_H_

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/core/json.h"

// Smooth, scalable text from TrueType fonts. A font is baked once at load time
// into a signed-distance-field atlas (stb_truetype), so the renderer can draw
// it crisp at any size with outlines, soft shadows and glows. Layout happens
// here with no GL: measuring, kerning and word wrapping in the overlay's
// logical pixels. The 5x7 bitmap font (font.h) is still used for text painted
// into textures.
namespace mythos {
// A font baked into a distance-field atlas. Move-only; the atlas is read by the
// renderer, which uploads it as a texture. Not thread-safe: kerning lookups
// fill mutable caches.
class SdfFont {
 public:
  // One baked glyph: where it sits in the atlas, in pixels, and where its box
  // goes relative to the pen on the baseline, all at the bake size.
  struct Glyph {
    // Atlas rectangle (empty for a glyph with no outline, such as a space).
    int ax = 0, ay = 0, aw = 0, ah = 0;
    // Offset of the box from the pen; y grows downwards.
    float x0 = 0, y0 = 0;
    // Horizontal pen advance.
    float advance = 0;
  };

  // Pixel height the font is baked at. Text sizes elsewhere are line heights
  // (ascent + descent) in pixels; metrics scale by size / kBakeSize.
  static constexpr float kBakeSize = 48.0f;

  // How far, in bake pixels, the distance field reaches outside a glyph: the
  // limit for outlines, shadows and glows.
  static constexpr int kSdfReach = 8;

  SdfFont() = default;

  SdfFont(const SdfFont&) = delete;

  SdfFont& operator=(const SdfFont&) = delete;

  SdfFont(SdfFont&&) noexcept;

  SdfFont& operator=(SdfFont&&) noexcept;

  ~SdfFont();

  // Reads a .ttf or .otf file through the vfs and bakes the Latin, Latin-1,
  // Greek and punctuation glyphs the font has. Returns false, with the reason
  // in `error` when not null, if the file cannot be read, is not a font, or has
  // none of those glyphs; the font is then left unchanged or unloaded.
  bool LoadFile(const std::string& path, std::string* error);

  // True once LoadFile has succeeded.
  bool Loaded() const { return info_ != nullptr; }

  // Distance from the top of the line to the baseline at text size `size`.
  float Ascent(const float size) const { return ascent_ * size / kBakeSize; }

  // Width of UTF-8 `text` at text size `size`, advances plus kerning.
  float Width(const std::string& text, float size) const;

  // Word-wraps `text` at spaces into lines no wider than `max_width` at text
  // size `size`. A word wider than the limit gets a line of its own and is not
  // broken. Runs of spaces collapse; there is no handling of newlines.
  std::vector<std::string> Wrap(const std::string& text, float size,
                                float max_width) const;

  // True when the font has a real glyph for codepoint `cp` (not the fallback).
  bool HasGlyph(const uint32_t cp) const { return glyphs_.contains(cp); }

  // The glyph to draw for `cp`: '?' when the font lacks it, an empty glyph when
  // it lacks that too.
  const Glyph& GlyphFor(uint32_t cp) const;

  // Kerning adjustment between two codepoints, in bake pixels (0 before a font
  // is loaded).
  float Kerning(uint32_t a, uint32_t b) const;

  // The atlas: a single-channel distance field, 128 on the outline and higher
  // inside, `AtlasWidth()` by `AtlasHeight()` bytes, row by row.
  int AtlasWidth() const { return atlas_w_; }
  int AtlasHeight() const { return atlas_h_; }
  const std::vector<uint8_t>& Atlas() const { return atlas_; }

 private:
  // Width of a run of codepoints in bake pixels: advances plus the kerning
  // between neighbors.
  float BakedWidth(const std::vector<uint32_t>& cps) const;

  // The stb_truetype state and the font file it reads from (owned).
  struct Info;
  Info* info_ = nullptr;
  // Font units to bake pixels.
  float scale_ = 1.0f;
  float ascent_ = 0.0f;
  std::unordered_map<uint32_t, Glyph> glyphs_;
  mutable std::unordered_map<uint64_t, float> kern_cache_;

  // Flat lookups for the commonest text: the glyph of each Latin-1 codepoint
  // (fallback already applied; null before a font is loaded) and the kerning of
  // each ASCII pair (NaN means not yet asked).
  std::array<const struct Glyph*, 256> latin_{};
  mutable std::vector<float> kern_ascii_;
  int atlas_w_ = 0, atlas_h_ = 0;
  std::vector<uint8_t> atlas_;
};

// Decodes UTF-8 into codepoints. Invalid or truncated sequences come out as
// '?', one per offending byte.
std::vector<uint32_t> Utf8Codepoints(const std::string& text);

// How a run of text is decorated. Distances are in the overlay's logical
// pixels; colors are 0xAARRGGBB.
struct TextEffect {
  // Width of a ring of `outline_color` around the letters; 0 for none.
  float outline = 0.0f;
  uint32_t outline_color = 0xFF000000;
  float shadow_x = 0.0f, shadow_y = 0.0f;
  // 0 means no shadow; larger is softer and wider.
  float shadow_blur = 0.0f;
  uint32_t shadow_color = 0x00000000;

  bool operator==(const TextEffect&) const = default;
};

// One line of text positioned in overlay pixels: (x, y) is the top-left of the
// line box and `size` the line height. `font` is not owned and must outlive the
// run.
struct TextRun {
  const SdfFont* font = nullptr;
  std::string text;
  float x = 0, y = 0, size = 10;
  uint32_t color = 0xFFFFFFFF;
  TextEffect fx;

  bool operator==(const TextRun&) const = default;
};

// Everything the overlay wants written this frame; the renderer draws it over
// the overlay.
struct TextLayer {
  std::vector<TextRun> runs;
  void Clear() { runs.clear(); }
};

// Loads the fonts a fonts.json names. For each (role, font) pair, the key
// `role` of `root` is the path of a font file under `asset_dir`. Every role is
// required; returns false with the reason in `error` (naming `file`) when one
// is missing or fails to load.
bool LoadFontFiles(const json::Value& root, const std::string& file,
                   const std::string& asset_dir,
                   const std::vector<std::pair<const char*, SdfFont*>>& roles,
                   std::string* error);
}

#endif
