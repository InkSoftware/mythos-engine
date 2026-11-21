#ifndef MYTHOS_ENGINE_GFX_FONT_H_
#define MYTHOS_ENGINE_GFX_FONT_H_

#include <string>
#include <vector>

#include "engine/gfx/surface.h"

// Old-school 5x7 bitmap font for text painted into surfaces and textures
// (smooth text is in text.h). Covers ASCII 32..95 plus the Greek capitals;
// lower case is drawn as upper case and Greek accents are dropped, as usual for
// all-caps Greek. Text is UTF-8.
namespace mythos {
// Glyph size in pixels, before scaling.
constexpr int kGlyphW = 5;
constexpr int kGlyphH = 7;

// Draws `text` onto `s` with its top-left at (x, y), in 0xAARRGGBB `color`.
// Each character takes a 6x8 pixel cell at scale 1 (glyph plus one pixel of
// spacing); `scale` (clamped to at least 1) enlarges every pixel to a square.
// Unknown characters draw as '?'. Pixels outside the surface are clipped.
void DrawText(Surface& s, int x, int y, const std::string& text, uint32_t color,
              int scale = 1);

// Number of characters (codepoints) in a UTF-8 string.
int TextLength(const std::string& text);

// Width in pixels of `text` drawn at `scale`, without the trailing spacing
// column.
int TextWidth(const std::string& text, int scale = 1);
}

#endif
