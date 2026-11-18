#include "engine/gfx/font.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

#include "engine/gfx/text.h"

namespace mythos {
namespace {

// The bitmap table starts at the space character and ends at '_' (ASCII 95).
constexpr int kFirstGlyphChar = 32;
constexpr int kLastGlyphChar = 95;
// Pixels from one character to the next at scale 1: the glyph plus one pixel of
// spacing.
constexpr int kCellAdvance = kGlyphW + 1;

// Column-major 5x7 glyphs, bit 0 = top row. Index = ASCII code - 32.
constexpr uint8_t kGlyphs[64][5] = {
    {0, 0, 0, 0, 0},                 // ' '
    {0, 0, 0x5F, 0, 0},              // !
    {0, 0x07, 0, 0x07, 0},           // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14},  // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},  // $
    {0x23, 0x13, 0x08, 0x64, 0x62},  // %
    {0x36, 0x49, 0x55, 0x22, 0x50},  // &
    {0, 0x05, 0x03, 0, 0},           // '
    {0, 0x1C, 0x22, 0x41, 0},        // (
    {0, 0x41, 0x22, 0x1C, 0},        // )
    {0x08, 0x2A, 0x1C, 0x2A, 0x08},  // *
    {0x08, 0x08, 0x3E, 0x08, 0x08},  // +
    {0, 0x50, 0x30, 0, 0},           // ,
    {0x08, 0x08, 0x08, 0x08, 0x08},  // -
    {0, 0x60, 0x60, 0, 0},           // .
    {0x20, 0x10, 0x08, 0x04, 0x02},  // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // 0
    {0, 0x42, 0x7F, 0x40, 0},        // 1
    {0x42, 0x61, 0x51, 0x49, 0x46},  // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31},  // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10},  // 4
    {0x27, 0x45, 0x45, 0x45, 0x39},  // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  // 6
    {0x01, 0x71, 0x09, 0x05, 0x03},  // 7
    {0x36, 0x49, 0x49, 0x49, 0x36},  // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E},  // 9
    {0, 0x36, 0x36, 0, 0},           // :
    {0, 0x56, 0x36, 0, 0},           // ;
    {0x08, 0x14, 0x22, 0x41, 0},     // <
    {0x14, 0x14, 0x14, 0x14, 0x14},  // =
    {0, 0x41, 0x22, 0x14, 0x08},     // >
    {0x02, 0x01, 0x51, 0x09, 0x06},  // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E},  // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E},  // A
    {0x7F, 0x49, 0x49, 0x49, 0x36},  // B
    {0x3E, 0x41, 0x41, 0x41, 0x22},  // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C},  // D
    {0x7F, 0x49, 0x49, 0x49, 0x41},  // E
    {0x7F, 0x09, 0x09, 0x09, 0x01},  // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A},  // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F},  // H
    {0x00, 0x41, 0x7F, 0x41, 0x00},  // I
    {0x20, 0x40, 0x41, 0x3F, 0x01},  // J
    {0x7F, 0x08, 0x14, 0x22, 0x41},  // K
    {0x7F, 0x40, 0x40, 0x40, 0x40},  // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},  // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F},  // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  // O
    {0x7F, 0x09, 0x09, 0x09, 0x06},  // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E},  // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46},  // R
    {0x46, 0x49, 0x49, 0x49, 0x31},  // S
    {0x01, 0x01, 0x7F, 0x01, 0x01},  // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F},  // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F},  // W
    {0x63, 0x14, 0x08, 0x14, 0x63},  // X
    {0x07, 0x08, 0x70, 0x08, 0x07},  // Y
    {0x61, 0x51, 0x49, 0x45, 0x43},  // Z
    {0, 0x7F, 0x41, 0x41, 0},        // [
    {0x02, 0x04, 0x08, 0x10, 0x20},  // backslash
    {0, 0x41, 0x41, 0x7F, 0},        // ]
    {0x04, 0x02, 0x01, 0x02, 0x04},  // ^
    {0x40, 0x40, 0x40, 0x40, 0x40},  // _
};

// Greek capitals that have no Latin look-alike, same format. Order:
// GAMMA DELTA THETA LAMDA XI PI SIGMA PHI PSI OMEGA
constexpr uint8_t kGreekGlyphs[10][5] = {
    {0x7F, 0x01, 0x01, 0x01, 0x01},  // GAMMA
    {0x70, 0x4C, 0x43, 0x4C, 0x70},  // DELTA
    {0x3E, 0x49, 0x49, 0x49, 0x3E},  // THETA
    {0x70, 0x0C, 0x03, 0x0C, 0x70},  // LAMDA
    {0x41, 0x49, 0x49, 0x49, 0x41},  // XI
    {0x7F, 0x01, 0x01, 0x01, 0x7F},  // PI
    {0x63, 0x55, 0x49, 0x41, 0x41},  // SIGMA
    {0x1C, 0x22, 0x7F, 0x22, 0x1C},  // PHI
    {0x07, 0x08, 0x7F, 0x08, 0x07},  // PSI
    {0x4E, 0x71, 0x01, 0x71, 0x4E},  // OMEGA
};

// Maps a Greek codepoint onto its capital letter (accents are dropped, as is
// usual for Greek in all caps). Returns 0 for non-Greek codepoints.
uint32_t GreekCapital(uint32_t cp) {
  switch (cp) {  // accented vowels and dialytika forms
    case 0x386:
    case 0x3AC:
      return 0x391;  // alpha
    case 0x388:
    case 0x3AD:
      return 0x395;  // epsilon
    case 0x389:
    case 0x3AE:
      return 0x397;  // eta
    case 0x38A:
    case 0x3AF:
    case 0x3CA:
    case 0x390:
      return 0x399;  // iota
    case 0x38C:
    case 0x3CC:
      return 0x39F;  // omicron
    case 0x38E:
    case 0x3CD:
    case 0x3CB:
    case 0x3B0:
      return 0x3A5;  // upsilon
    case 0x38F:
    case 0x3CE:
      return 0x3A9;  // omega
    case 0x3C2:
      return 0x3A3;  // final sigma
    default:
      break;
  }
  if (cp >= 0x391 && cp <= 0x3A9) return cp;         // capitals
  if (cp >= 0x3B1 && cp <= 0x3C9) return cp - 0x20;  // small letters
  return 0;
}

const uint8_t* GlyphFor(uint32_t cp) {
  const uint32_t greek = GreekCapital(cp);
  if (greek != 0) {
    switch (greek) {  // capitals that share their shape with a Latin letter
      case 0x391:
        return kGlyphs['A' - 32];
      case 0x392:
        return kGlyphs['B' - 32];
      case 0x395:
        return kGlyphs['E' - 32];
      case 0x396:
        return kGlyphs['Z' - 32];
      case 0x397:
        return kGlyphs['H' - 32];
      case 0x399:
        return kGlyphs['I' - 32];
      case 0x39A:
        return kGlyphs['K' - 32];
      case 0x39C:
        return kGlyphs['M' - 32];
      case 0x39D:
        return kGlyphs['N' - 32];
      case 0x39F:
        return kGlyphs['O' - 32];
      case 0x3A1:
        return kGlyphs['P' - 32];
      case 0x3A4:
        return kGlyphs['T' - 32];
      case 0x3A5:
        return kGlyphs['Y' - 32];
      case 0x3A7:
        return kGlyphs['X' - 32];
      case 0x393:
        return kGreekGlyphs[0];
      case 0x394:
        return kGreekGlyphs[1];
      case 0x398:
        return kGreekGlyphs[2];
      case 0x39B:
        return kGreekGlyphs[3];
      case 0x39E:
        return kGreekGlyphs[4];
      case 0x3A0:
        return kGreekGlyphs[5];
      case 0x3A3:
        return kGreekGlyphs[6];
      case 0x3A6:
        return kGreekGlyphs[7];
      case 0x3A8:
        return kGreekGlyphs[8];
      case 0x3A9:
        return kGreekGlyphs[9];
      default:
        break;
    }
  }
  int ascii = cp < 128 ? std::toupper(static_cast<int>(cp)) : '?';
  if (ascii < kFirstGlyphChar || ascii > kLastGlyphChar) ascii = '?';
  return kGlyphs[ascii - kFirstGlyphChar];
}

}  // namespace

void DrawText(Surface& s, int x, int y, const std::string& text, uint32_t color,
              int scale) {
  scale = std::max(scale, 1);
  int pen_x = x;
  for (const uint32_t cp : Utf8Codepoints(text)) {
    const uint8_t* columns = GlyphFor(cp);
    for (int col = 0; col < kGlyphW; ++col) {
      for (int row = 0; row < kGlyphH; ++row) {
        if (!((columns[col] >> row) & 1)) continue;
        // Each lit bit becomes a scale x scale block of pixels.
        for (int dy = 0; dy < scale; ++dy) {
          for (int dx = 0; dx < scale; ++dx) {
            s.Put(pen_x + col * scale + dx, y + row * scale + dy, color);
          }
        }
      }
    }
    pen_x += kCellAdvance * scale;
  }
}

int TextLength(const std::string& text) {
  return static_cast<int>(Utf8Codepoints(text).size());
}

int TextWidth(const std::string& text, int scale) {
  const int n = TextLength(text);
  if (n == 0) return 0;
  return n * kCellAdvance * scale -
         scale;  // no spacing after the last character
}

std::vector<std::string> WrapText(const std::string& text, size_t max_chars) {
  std::vector<std::string> lines;
  std::string line;
  size_t line_len =
      0;  // in characters, not bytes (Greek letters are two bytes)
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = text.find(' ', start);
    const std::string word = text.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    if (!word.empty()) {
      const size_t word_len = static_cast<size_t>(TextLength(word));
      if (!line.empty() && line_len + 1 + word_len > max_chars) {
        lines.push_back(line);
        line.clear();
        line_len = 0;
      }
      if (!line.empty()) {
        line += ' ';
        ++line_len;
      }
      line += word;
      line_len += word_len;
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

}  // namespace mythos
