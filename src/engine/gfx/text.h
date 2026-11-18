#ifndef MYTHOS_ENGINE_GFX_TEXT_H_
#define MYTHOS_ENGINE_GFX_TEXT_H_
// Smooth, scalable text from TrueType fonts. A font is baked once at load time
// into a signed-distance-field atlas (stb_truetype), so the renderer can draw
// it crisp at any size with outlines, soft shadows and glows. Layout happens
// here, with no GL: measuring, kerning and word wrapping in the overlay's
// logical pixels. The 5x7 bitmap font (font.h) is still used for text painted
// into textures.

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/core/json.h"

namespace mythos {

class SdfFont {
 public:
  // One baked glyph: where it sits in the atlas (pixels) and where its box goes
  // relative to the pen on the baseline, all at the bake size.
  struct Glyph {
    int ax = 0, ay = 0, aw = 0, ah = 0;  // atlas rectangle
    float x0 = 0, y0 = 0;                // box offset from the pen (y down)
    float advance = 0;
  };

  // Text sizes are the font's line height (ascent + descent) in pixels.
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

  // Reads a .ttf/.otf file and bakes the Latin, Latin-1, Greek and punctuation
  // glyphs.
  bool LoadFile(const std::string& path, std::string* error);
  bool Loaded() const { return info_ != nullptr; }

  float Ascent(float size) const { return ascent_ * size / kBakeSize; }
  float Width(const std::string& text, float size) const;
  // Word-wraps text into lines no wider than maxWidth (a long word gets a line
  // of its own).
  std::vector<std::string> Wrap(const std::string& text, float size,
                                float max_width) const;
  // True when the font has a real glyph for codepoint cp (not the fallback
  // box).
  bool HasGlyph(uint32_t cp) const { return glyphs_.contains(cp); }

  // The glyph to draw for cp ('?' when the font lacks it), and the kerning
  // between a pair.
  const Glyph& GlyphFor(uint32_t cp) const;
  float Kerning(uint32_t a, uint32_t b) const;  // in bake pixels

  // Single-channel distance field: 128 on the outline, more inside.
  int AtlasWidth() const { return atlas_w_; }
  int AtlasHeight() const { return atlas_h_; }
  const std::vector<uint8_t>& Atlas() const { return atlas_; }

 private:
  // Width of a run of codepoints in bake pixels: advances plus the kerning
  // between neighbours.
  float BakedWidth(const std::vector<uint32_t>& cps) const;

  struct Info;
  Info* info_ =
      nullptr;  // the stb_truetype state and the font file it reads from
  float scale_ = 1.0f;   // font units -> bake pixels
  float ascent_ = 0.0f;  // in bake pixels
  std::unordered_map<uint32_t, Glyph> glyphs_;
  mutable std::unordered_map<uint64_t, float> kern_cache_;
  // Flat lookups for the commonest text: the glyph of each Latin-1 codepoint
  // (the fallback already applied; null before a font is loaded) and the
  // kerning of each ASCII pair (NaN = not yet asked).
  std::array<const struct Glyph*, 256> latin_{};
  mutable std::vector<float> kern_ascii_;
  int atlas_w_ = 0, atlas_h_ = 0;
  std::vector<uint8_t> atlas_;
};

// Decodes UTF-8 into codepoints (invalid bytes come out as '?').
std::vector<uint32_t> Utf8Codepoints(const std::string& text);

// How a run of text is decorated. Distances are in the overlay's logical
// pixels.
struct TextEffect {
  float outline = 0.0f;  // a ring of outlineColor around the letters
  uint32_t outline_color = 0xFF000000;
  float shadow_x = 0.0f, shadow_y = 0.0f;
  float shadow_blur = 0.0f;  // 0 = no shadow; larger = softer and wider
  uint32_t shadow_color = 0x00000000;
  bool operator==(const TextEffect&) const = default;
};

// One line of text, positioned in overlay pixels: (x, y) is the top-left of the
// line box. Colours are 0xAARRGGBB, like Surface pixels.
struct TextRun {
  const SdfFont* font = nullptr;
  std::string text;
  float x = 0, y = 0, size = 10;
  uint32_t color = 0xFFFFFFFF;
  TextEffect fx;
  bool operator==(const TextRun&) const = default;
};

// Everything the overlay wants written this frame, drawn by the renderer over
// the overlay.
struct TextLayer {
  std::vector<TextRun> runs;
  void Clear() { runs.clear(); }
};

// Loads the fonts a fonts.json names: for each role (a key of the file, e.g.
// "title"), the path of a font file under assetDir. Every role is required.
bool LoadFontFiles(const json::Value& root, const std::string& file,
                   const std::string& asset_dir,
                   const std::vector<std::pair<const char*, SdfFont*>>& roles,
                   std::string* error);

}  // namespace mythos

#endif  // MYTHOS_ENGINE_GFX_TEXT_H_
