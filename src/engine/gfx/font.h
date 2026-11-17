#pragma once
// Old-school 5x7 bitmap font. ASCII 32..95 plus the Greek alphabet (UTF-8); lower case
// is mapped to upper case, and Greek accents are dropped, as usual for all-caps Greek.

#include <string>
#include <vector>

#include "engine/gfx/surface.h"

namespace mythos {

constexpr int kGlyphW = 5;
constexpr int kGlyphH = 7;

// Draws text onto s. Scale 1 = 6x8 pixels per character (incl. spacing).
void drawText(Surface& s, int x, int y, const std::string& text, uint32_t color, int scale = 1);

// Number of characters (codepoints) in a UTF-8 string.
int textLength(const std::string& text);

// Pixel width of text when drawn at the given scale.
int textWidth(const std::string& text, int scale = 1);

// Word-wraps text into lines of at most maxChars characters.
std::vector<std::string> wrapText(const std::string& text, size_t maxChars);

}  // namespace mythos
