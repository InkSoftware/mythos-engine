#ifndef MYTHOS_ENGINE_GFX_FONT_H_
#define MYTHOS_ENGINE_GFX_FONT_H_
// Old-school 5x7 bitmap font. ASCII 32..95 plus the Greek alphabet (UTF-8);
// lower case is mapped to upper case, and Greek accents are dropped, as usual
// for all-caps Greek.

#include <string>
#include <vector>

#include "engine/gfx/surface.h"

namespace mythos {

constexpr int kGlyphW = 5;
constexpr int kGlyphH = 7;

// Draws text onto s. Scale 1 = 6x8 pixels per character (incl. spacing).
void DrawText(Surface& s, int x, int y, const std::string& text, uint32_t color,
              int scale = 1);

// Number of characters (codepoints) in a UTF-8 string.
int TextLength(const std::string& text);

// Pixel width of text when drawn at the given scale.
int TextWidth(const std::string& text, int scale = 1);

// Word-wraps text into lines of at most maxChars characters.
std::vector<std::string> WrapText(const std::string& text, size_t max_chars);

}  // namespace mythos

#endif  // MYTHOS_ENGINE_GFX_FONT_H_
