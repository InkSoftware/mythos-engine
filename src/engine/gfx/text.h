#pragma once
// Smooth, scalable text from TrueType fonts. A font is baked once at load time into a
// signed-distance-field atlas (stb_truetype), so the renderer can draw it crisp at any size
// with outlines, soft shadows and glows. Layout happens here, with no GL: measuring,
// kerning and word wrapping in the overlay's logical pixels. The 5x7 bitmap font
// (font.h) is still used for text painted into textures.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/core/json.h"

namespace mythos {

class SdfFont {
public:
    // One baked glyph: where it sits in the atlas (pixels) and where its box goes relative
    // to the pen on the baseline, all at the bake size.
    struct Glyph {
        int ax = 0, ay = 0, aw = 0, ah = 0;  // atlas rectangle
        float x0 = 0, y0 = 0;                // box offset from the pen (y down)
        float advance = 0;
    };

    // Text sizes are the font's line height (ascent + descent) in pixels.
    static constexpr float kBakeSize = 48.0f;
    // How far, in bake pixels, the distance field reaches outside a glyph: the limit for
    // outlines, shadows and glows.
    static constexpr int kSdfReach = 8;

    SdfFont() = default;
    SdfFont(const SdfFont&) = delete;
    SdfFont& operator=(const SdfFont&) = delete;
    SdfFont(SdfFont&&) noexcept;
    SdfFont& operator=(SdfFont&&) noexcept;
    ~SdfFont();

    // Reads a .ttf/.otf file and bakes the Latin, Latin-1, Greek and punctuation glyphs.
    bool loadFile(const std::string& path, std::string* error);
    bool loaded() const { return info_ != nullptr; }

    float ascent(float size) const { return ascent_ * size / kBakeSize; }
    float width(const std::string& text, float size) const;
    // Word-wraps text into lines no wider than maxWidth (a long word gets a line of its own).
    std::vector<std::string> wrap(const std::string& text, float size, float maxWidth) const;
    // True when the font has a real glyph for codepoint cp (not the fallback box).
    bool hasGlyph(uint32_t cp) const { return glyphs_.count(cp) != 0; }

    // The glyph to draw for cp ('?' when the font lacks it), and the kerning between a pair.
    const Glyph& glyph(uint32_t cp) const;
    float kerning(uint32_t a, uint32_t b) const;  // in bake pixels

    // Single-channel distance field: 128 on the outline, more inside.
    int atlasWidth() const { return atlasW_; }
    int atlasHeight() const { return atlasH_; }
    const std::vector<uint8_t>& atlas() const { return atlas_; }

private:
    struct Info;
    Info* info_ = nullptr;  // the stb_truetype state and the font file it reads from
    float scale_ = 1.0f;     // font units -> bake pixels
    float ascent_ = 0.0f;    // in bake pixels
    std::unordered_map<uint32_t, Glyph> glyphs_;
    mutable std::unordered_map<uint64_t, float> kernCache_;
    int atlasW_ = 0, atlasH_ = 0;
    std::vector<uint8_t> atlas_;
};

// Decodes UTF-8 into codepoints (invalid bytes come out as '?').
std::vector<uint32_t> utf8Codepoints(const std::string& text);

// How a run of text is decorated. Distances are in the overlay's logical pixels.
struct TextEffect {
    float outline = 0.0f;           // a ring of outlineColor around the letters
    uint32_t outlineColor = 0xFF000000;
    float shadowX = 0.0f, shadowY = 0.0f;
    float shadowBlur = 0.0f;        // 0 = no shadow; larger = softer and wider
    uint32_t shadowColor = 0x00000000;
};

// One line of text, positioned in overlay pixels: (x, y) is the top-left of the line box.
// Colours are 0xAARRGGBB, like Surface pixels.
struct TextRun {
    const SdfFont* font = nullptr;
    std::string text;
    float x = 0, y = 0, size = 10;
    uint32_t color = 0xFFFFFFFF;
    TextEffect fx;
};

// Everything the overlay wants written this frame, drawn by the renderer over the overlay.
struct TextLayer {
    std::vector<TextRun> runs;
    void clear() { runs.clear(); }
};

// Loads the fonts a fonts.json names: for each role (a key of the file, e.g. "title"), the path
// of a font file under assetDir. Every role is required.
bool loadFontFiles(const json::Value& root, const std::string& file, const std::string& assetDir,
                   const std::vector<std::pair<const char*, SdfFont*>>& roles, std::string* error);

}  // namespace mythos
