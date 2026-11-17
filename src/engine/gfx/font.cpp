#include "engine/gfx/font.h"

#include <cctype>
#include <cstdint>

namespace mythos {
namespace {

// Column-major 5x7 glyphs, bit 0 = top row. Index = ASCII code - 32.
constexpr uint8_t kGlyphs[64][5] = {
    {0, 0, 0, 0, 0},            // ' '
    {0, 0, 0x5F, 0, 0},         // !
    {0, 0x07, 0, 0x07, 0},      // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14},  // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},  // $
    {0x23, 0x13, 0x08, 0x64, 0x62},  // %
    {0x36, 0x49, 0x55, 0x22, 0x50},  // &
    {0, 0x05, 0x03, 0, 0},      // '
    {0, 0x1C, 0x22, 0x41, 0},   // (
    {0, 0x41, 0x22, 0x1C, 0},   // )
    {0x08, 0x2A, 0x1C, 0x2A, 0x08},  // *
    {0x08, 0x08, 0x3E, 0x08, 0x08},  // +
    {0, 0x50, 0x30, 0, 0},      // ,
    {0x08, 0x08, 0x08, 0x08, 0x08},  // -
    {0, 0x60, 0x60, 0, 0},      // .
    {0x20, 0x10, 0x08, 0x04, 0x02},  // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // 0
    {0, 0x42, 0x7F, 0x40, 0},   // 1
    {0x42, 0x61, 0x51, 0x49, 0x46},  // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31},  // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10},  // 4
    {0x27, 0x45, 0x45, 0x45, 0x39},  // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  // 6
    {0x01, 0x71, 0x09, 0x05, 0x03},  // 7
    {0x36, 0x49, 0x49, 0x49, 0x36},  // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E},  // 9
    {0, 0x36, 0x36, 0, 0},      // :
    {0, 0x56, 0x36, 0, 0},      // ;
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

// Maps a Greek codepoint onto its capital letter (accents are dropped, as is usual
// for Greek in all caps). Returns 0 for non-Greek codepoints.
uint32_t greekCapital(uint32_t cp) {
    switch (cp) {  // accented vowels and dialytika forms
        case 0x386: case 0x3AC: return 0x391;               // alpha
        case 0x388: case 0x3AD: return 0x395;               // epsilon
        case 0x389: case 0x3AE: return 0x397;               // eta
        case 0x38A: case 0x3AF: case 0x3CA: case 0x390: return 0x399;  // iota
        case 0x38C: case 0x3CC: return 0x39F;               // omicron
        case 0x38E: case 0x3CD: case 0x3CB: case 0x3B0: return 0x3A5;  // upsilon
        case 0x38F: case 0x3CE: return 0x3A9;               // omega
        case 0x3C2: return 0x3A3;                           // final sigma
        default: break;
    }
    if (cp >= 0x391 && cp <= 0x3A9) return cp;              // capitals
    if (cp >= 0x3B1 && cp <= 0x3C9) return cp - 0x20;       // small letters
    return 0;
}

const uint8_t* glyphFor(uint32_t cp) {
    const uint32_t greek = greekCapital(cp);
    if (greek != 0) {
        switch (greek) {  // capitals that share their shape with a Latin letter
            case 0x391: return kGlyphs['A' - 32];
            case 0x392: return kGlyphs['B' - 32];
            case 0x395: return kGlyphs['E' - 32];
            case 0x396: return kGlyphs['Z' - 32];
            case 0x397: return kGlyphs['H' - 32];
            case 0x399: return kGlyphs['I' - 32];
            case 0x39A: return kGlyphs['K' - 32];
            case 0x39C: return kGlyphs['M' - 32];
            case 0x39D: return kGlyphs['N' - 32];
            case 0x39F: return kGlyphs['O' - 32];
            case 0x3A1: return kGlyphs['P' - 32];
            case 0x3A4: return kGlyphs['T' - 32];
            case 0x3A5: return kGlyphs['Y' - 32];
            case 0x3A7: return kGlyphs['X' - 32];
            case 0x393: return kGreekGlyphs[0];
            case 0x394: return kGreekGlyphs[1];
            case 0x398: return kGreekGlyphs[2];
            case 0x39B: return kGreekGlyphs[3];
            case 0x39E: return kGreekGlyphs[4];
            case 0x3A0: return kGreekGlyphs[5];
            case 0x3A3: return kGreekGlyphs[6];
            case 0x3A6: return kGreekGlyphs[7];
            case 0x3A8: return kGreekGlyphs[8];
            case 0x3A9: return kGreekGlyphs[9];
            default: break;
        }
    }
    int c = cp < 128 ? std::toupper(static_cast<int>(cp)) : '?';
    if (c < 32 || c > 95) c = '?';
    return kGlyphs[c - 32];
}

// Decodes UTF-8 (anything malformed becomes '?'). ASCII passes straight through.
std::vector<uint32_t> codepoints(const std::string& text) {
    std::vector<uint32_t> out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const auto b0 = static_cast<unsigned char>(text[i]);
        uint32_t cp = '?';
        size_t len = 1;
        if (b0 < 0x80) {
            cp = b0;
        } else if ((b0 & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cp = (static_cast<uint32_t>(b0 & 0x1F) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            len = 2;
        } else if ((b0 & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cp = (static_cast<uint32_t>(b0 & 0x0F) << 12) |
                 ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            len = 3;
        } else if ((b0 & 0xF8) == 0xF0 && i + 3 < text.size()) {
            len = 4;  // outside the font's range anyway
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

}  // namespace

void drawText(Surface& s, int x, int y, const std::string& text, uint32_t color, int scale) {
    if (scale < 1) scale = 1;
    int cx = x;
    for (uint32_t cp : codepoints(text)) {
        const uint8_t* g = glyphFor(cp);
        for (int col = 0; col < kGlyphW; ++col) {
            for (int row = 0; row < kGlyphH; ++row) {
                if (!((g[col] >> row) & 1)) continue;
                for (int dy = 0; dy < scale; ++dy)
                    for (int dx = 0; dx < scale; ++dx)
                        s.put(cx + col * scale + dx, y + row * scale + dy, color);
            }
        }
        cx += 6 * scale;
    }
}

int textLength(const std::string& text) { return static_cast<int>(codepoints(text).size()); }

int textWidth(const std::string& text, int scale) {
    const int n = textLength(text);
    if (n == 0) return 0;
    return n * 6 * scale - scale;
}

std::vector<std::string> wrapText(const std::string& text, size_t maxChars) {
    std::vector<std::string> lines;
    std::string line;
    size_t lineLen = 0;  // in characters, not bytes (Greek letters are two bytes)
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(' ', start);
        std::string word = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!word.empty()) {
            const size_t wordLen = static_cast<size_t>(textLength(word));
            if (!line.empty() && lineLen + 1 + wordLen > maxChars) {
                lines.push_back(line);
                line.clear();
                lineLen = 0;
            }
            if (!line.empty()) {
                line += ' ';
                ++lineLen;
            }
            line += word;
            lineLen += wordLen;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

}  // namespace mythos
