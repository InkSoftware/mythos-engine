#include "engine/gfx/text.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#include "engine/core/vfs.h"
#include "stb_truetype.h"

namespace mythos {

struct SdfFont::Info {
    std::vector<unsigned char> data;  // stb_truetype reads the file in place, so it must stay alive
    stbtt_fontinfo font{};
};

namespace {

// The characters the game can show: ASCII, Latin-1, monotonic Greek and common punctuation.
std::vector<uint32_t> bakeSet() {
    std::vector<uint32_t> cps;
    for (uint32_t c = 0x20; c <= 0x7E; ++c) cps.push_back(c);
    for (uint32_t c = 0xA0; c <= 0xFF; ++c) cps.push_back(c);
    for (uint32_t c = 0x384; c <= 0x3CE; ++c) cps.push_back(c);
    for (uint32_t c = 0x2010; c <= 0x2026; ++c) cps.push_back(c);
    cps.push_back(0x2039);
    cps.push_back(0x203A);
    return cps;
}

// Atlas layout: glyphs are packed left to right in rows, one pixel apart.
constexpr int kAtlasWidth = 1024;
constexpr int kAtlasPadding = 1;
// stb_truetype's SDF: the value on the outline (the renderer thresholds at 0.5) and its slope.
constexpr unsigned char kSdfOnEdge = 128;
constexpr float kSdfByteRange = 128.0f;  // distance values span 128 either side of the edge

// A glyph's metrics and its distance-field pixels, before it has a place in the atlas.
struct BakedGlyph {
    uint32_t codepoint;
    SdfFont::Glyph glyph;
    std::vector<uint8_t> pixels;
};

// Bakes every glyph of bakeSet() the font has into a distance field.
std::vector<BakedGlyph> bakeGlyphs(const stbtt_fontinfo* font, float scale) {
    std::vector<BakedGlyph> baked;
    const float distanceScale = kSdfByteRange / SdfFont::kSdfReach;
    for (uint32_t cp : bakeSet()) {
        if (!stbtt_FindGlyphIndex(font, static_cast<int>(cp))) continue;
        BakedGlyph entry{cp, {}, {}};
        int advance = 0, leftBearing = 0;
        stbtt_GetCodepointHMetrics(font, static_cast<int>(cp), &advance, &leftBearing);
        entry.glyph.advance = advance * scale;
        int width = 0, height = 0, xOffset = 0, yOffset = 0;
        unsigned char* sdf = stbtt_GetCodepointSDF(font, scale, static_cast<int>(cp), SdfFont::kSdfReach, kSdfOnEdge,
                                                   distanceScale, &width, &height, &xOffset, &yOffset);
        if (sdf) {
            entry.glyph.aw = width;
            entry.glyph.ah = height;
            entry.glyph.x0 = static_cast<float>(xOffset);
            entry.glyph.y0 = static_cast<float>(yOffset);
            entry.pixels.assign(sdf, sdf + static_cast<size_t>(width) * height);
            stbtt_FreeSDF(sdf, nullptr);
        }
        baked.push_back(std::move(entry));
    }
    return baked;
}

// Gives each glyph its atlas rectangle (rows of kAtlasWidth) and returns the atlas height.
int packAtlasRows(std::vector<BakedGlyph>& baked) {
    int penX = kAtlasPadding, penY = kAtlasPadding, rowHeight = 0;
    for (auto& entry : baked) {
        if (entry.glyph.aw == 0) continue;  // a space has no pixels
        if (penX + entry.glyph.aw + kAtlasPadding > kAtlasWidth) {  // start a new row
            penX = kAtlasPadding;
            penY += rowHeight + kAtlasPadding;
            rowHeight = 0;
        }
        entry.glyph.ax = penX;
        entry.glyph.ay = penY;
        penX += entry.glyph.aw + kAtlasPadding;
        rowHeight = std::max(rowHeight, entry.glyph.ah);
    }
    return penY + rowHeight + kAtlasPadding;
}

}  // namespace

std::vector<uint32_t> utf8Codepoints(const std::string& text) {
    std::vector<uint32_t> out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const auto b0 = static_cast<unsigned char>(text[i]);
        // The low six bits of the k-th continuation byte after the lead byte.
        auto cont = [&](size_t k) { return static_cast<uint32_t>(static_cast<unsigned char>(text[i + k]) & 0x3F); };
        if (b0 < 0x80) {
            out.push_back(b0);
            i += 1;
        } else if ((b0 & 0xE0) == 0xC0 && i + 1 < text.size()) {
            out.push_back((static_cast<uint32_t>(b0 & 0x1F) << 6) | cont(1));
            i += 2;
        } else if ((b0 & 0xF0) == 0xE0 && i + 2 < text.size()) {
            out.push_back((static_cast<uint32_t>(b0 & 0x0F) << 12) | (cont(1) << 6) | cont(2));
            i += 3;
        } else if ((b0 & 0xF8) == 0xF0 && i + 3 < text.size()) {
            out.push_back((static_cast<uint32_t>(b0 & 0x07) << 18) | (cont(1) << 12) | (cont(2) << 6) | cont(3));
            i += 4;
        } else {
            out.push_back('?');
            i += 1;
        }
    }
    return out;
}

SdfFont::SdfFont(SdfFont&& other) noexcept { *this = std::move(other); }

SdfFont& SdfFont::operator=(SdfFont&& other) noexcept {
    if (this == &other) return *this;
    delete info_;
    info_ = std::exchange(other.info_, nullptr);
    scale_ = other.scale_;
    ascent_ = other.ascent_;
    glyphs_ = std::move(other.glyphs_);
    kernCache_ = std::move(other.kernCache_);
    latin_ = other.latin_;  // points into glyphs_'s nodes, which the move keeps
    kernAscii_ = std::move(other.kernAscii_);
    atlasW_ = other.atlasW_;
    atlasH_ = other.atlasH_;
    atlas_ = std::move(other.atlas_);
    return *this;
}

SdfFont::~SdfFont() { delete info_; }

bool SdfFont::loadFile(const std::string& path, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = path + ": " + why;
        return false;
    };
    std::string bytes;
    if (!vfs::readFile(path, bytes)) return fail("cannot open the font");
    auto info = new Info;
    info->data.assign(bytes.begin(), bytes.end());
    const int offset = stbtt_GetFontOffsetForIndex(info->data.data(), 0);
    if (info->data.empty() || offset < 0 || !stbtt_InitFont(&info->font, info->data.data(), offset)) {
        delete info;
        return fail("not a TrueType/OpenType font");
    }
    delete info_;
    info_ = info;
    glyphs_.clear();
    kernCache_.clear();
    kernAscii_.clear();
    latin_.fill(nullptr);

    const stbtt_fontinfo* fi = &info_->font;
    scale_ = stbtt_ScaleForPixelHeight(fi, kBakeSize);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(fi, &asc, &desc, &gap);
    ascent_ = asc * scale_;

    // Bake each glyph's distance field, then pack them into rows of the atlas.
    std::vector<BakedGlyph> baked = bakeGlyphs(fi, scale_);
    if (baked.empty()) return fail("has none of the game's characters");

    atlasW_ = kAtlasWidth;
    atlasH_ = packAtlasRows(baked);
    atlas_.assign(static_cast<size_t>(atlasW_) * atlasH_, 0);
    for (auto& entry : baked) {
        const Glyph& g = entry.glyph;
        for (int y = 0; y < g.ah; ++y)
            std::copy_n(entry.pixels.begin() + static_cast<std::ptrdiff_t>(y) * g.aw, g.aw,
                        atlas_.begin() + static_cast<std::ptrdiff_t>(g.ay + y) * atlasW_ + g.ax);
        glyphs_[entry.codepoint] = g;
    }
    static const Glyph none{};
    const auto fallback = glyphs_.find('?');
    for (uint32_t cp = 0; cp < 256; ++cp) {
        const auto it = glyphs_.find(cp);
        latin_[cp] = it != glyphs_.end() ? &it->second : fallback != glyphs_.end() ? &fallback->second : &none;
    }
    return true;
}

const SdfFont::Glyph& SdfFont::glyph(uint32_t cp) const {
    if (cp < 256 && latin_[cp]) return *latin_[cp];
    auto it = glyphs_.find(cp);
    if (it == glyphs_.end()) it = glyphs_.find('?');
    static const Glyph none{};
    return it != glyphs_.end() ? it->second : none;
}

float SdfFont::kerning(uint32_t a, uint32_t b) const {
    if (!info_) return 0.0f;
    float* slot = nullptr;
    constexpr uint32_t kAscii = 128;
    if (a < kAscii && b < kAscii) {
        if (kernAscii_.empty()) kernAscii_.assign(kAscii * kAscii, std::nanf(""));
        slot = &kernAscii_[a * kAscii + b];
        if (!std::isnan(*slot)) return *slot;
    }
    const uint64_t key = (static_cast<uint64_t>(a) << 32) | b;
    const auto it = kernCache_.find(key);
    if (it != kernCache_.end()) return it->second;
    const float kern = stbtt_GetCodepointKernAdvance(&info_->font, static_cast<int>(a), static_cast<int>(b)) * scale_;
    kernCache_.emplace(key, kern);
    if (slot) *slot = kern;
    return kern;
}

float SdfFont::bakedWidth(const std::vector<uint32_t>& cps) const {
    float width = 0.0f;
    for (size_t i = 0; i < cps.size(); ++i) {
        width += glyph(cps[i]).advance;
        if (i + 1 < cps.size()) width += kerning(cps[i], cps[i + 1]);
    }
    return width;
}

float SdfFont::width(const std::string& text, float size) const {
    return bakedWidth(utf8Codepoints(text)) * size / kBakeSize;
}

std::vector<std::string> SdfFont::wrap(const std::string& text, float size, float maxWidth) const {
    std::vector<std::string> lines;
    std::string line;
    // The line's width in bake pixels, kept as it grows: a candidate is the line, a space and the
    // word, so its width is the line's plus the space, the two kerns at the joins and the word's.
    float lineW = 0.0f;
    uint32_t lineLast = 0;
    const float space = glyph(' ').advance;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(' ', start);
        const std::string word = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!word.empty()) {
            const std::vector<uint32_t> cps = utf8Codepoints(word);
            const float wordW = bakedWidth(cps);
            if (line.empty()) {
                line = word;
                lineW = wordW;
            } else {
                const float candidateW = lineW + kerning(lineLast, ' ') + space + kerning(' ', cps.front()) + wordW;
                if (candidateW * size / kBakeSize > maxWidth) {
                    lines.push_back(line);
                    line = word;
                    lineW = wordW;
                } else {
                    line += ' ';
                    line += word;
                    lineW = candidateW;
                }
            }
            lineLast = cps.back();
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

bool loadFontFiles(const json::Value& root, const std::string& file, const std::string& assetDir,
                   const std::vector<std::pair<const char*, SdfFont*>>& roles, std::string* error) {
    for (const auto& [key, font] : roles) {
        const json::Value* path = json::find(root, key);
        if (!path || !path->is_string()) {
            if (error) *error = file + ": missing \"" + key + "\" (a font file under assets/)";
            return false;
        }
        if (!font->loadFile(assetDir + "/" + path->get<std::string>(), error)) return false;
    }
    return true;
}

}  // namespace mythos
