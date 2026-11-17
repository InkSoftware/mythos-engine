#include "engine/gfx/text.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
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

}  // namespace

std::vector<uint32_t> utf8Codepoints(const std::string& text) {
    std::vector<uint32_t> out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const auto b0 = static_cast<unsigned char>(text[i]);
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

SdfFont::SdfFont(SdfFont&& o) noexcept { *this = std::move(o); }

SdfFont& SdfFont::operator=(SdfFont&& o) noexcept {
    if (this == &o) return *this;
    delete info_;
    info_ = std::exchange(o.info_, nullptr);
    scale_ = o.scale_;
    ascent_ = o.ascent_;
    glyphs_ = std::move(o.glyphs_);
    kernCache_ = std::move(o.kernCache_);
    atlasW_ = o.atlasW_;
    atlasH_ = o.atlasH_;
    atlas_ = std::move(o.atlas_);
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

    const stbtt_fontinfo* fi = &info_->font;
    scale_ = stbtt_ScaleForPixelHeight(fi, kBakeSize);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(fi, &asc, &desc, &gap);
    ascent_ = asc * scale_;

    // Bake each glyph's distance field, then pack them into rows of a 1024-wide atlas.
    struct Baked {
        uint32_t cp;
        Glyph g;
        std::vector<uint8_t> px;
    };
    std::vector<Baked> baked;
    const float distScale = 128.0f / kSdfReach;
    for (uint32_t cp : bakeSet()) {
        if (!stbtt_FindGlyphIndex(fi, static_cast<int>(cp))) continue;
        Baked b{cp, {}, {}};
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(fi, static_cast<int>(cp), &adv, &lsb);
        b.g.advance = adv * scale_;
        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char* sdf = stbtt_GetCodepointSDF(fi, scale_, static_cast<int>(cp), kSdfReach, 128, distScale, &w,
                                                   &h, &xoff, &yoff);
        if (sdf) {
            b.g.aw = w;
            b.g.ah = h;
            b.g.x0 = static_cast<float>(xoff);
            b.g.y0 = static_cast<float>(yoff);
            b.px.assign(sdf, sdf + static_cast<size_t>(w) * h);
            stbtt_FreeSDF(sdf, nullptr);
        }
        baked.push_back(std::move(b));
    }
    if (baked.empty()) return fail("has none of the game's characters");

    atlasW_ = 1024;
    int penX = 1, penY = 1, rowH = 0;
    for (auto& b : baked) {
        if (b.g.aw == 0) continue;
        if (penX + b.g.aw + 1 > atlasW_) {
            penX = 1;
            penY += rowH + 1;
            rowH = 0;
        }
        b.g.ax = penX;
        b.g.ay = penY;
        penX += b.g.aw + 1;
        rowH = std::max(rowH, b.g.ah);
    }
    atlasH_ = penY + rowH + 1;
    atlas_.assign(static_cast<size_t>(atlasW_) * atlasH_, 0);
    for (auto& b : baked) {
        for (int y = 0; y < b.g.ah; ++y)
            std::copy_n(b.px.begin() + static_cast<std::ptrdiff_t>(y) * b.g.aw, b.g.aw,
                        atlas_.begin() + static_cast<std::ptrdiff_t>(b.g.ay + y) * atlasW_ + b.g.ax);
        glyphs_[b.cp] = b.g;
    }
    return true;
}

const SdfFont::Glyph& SdfFont::glyph(uint32_t cp) const {
    auto it = glyphs_.find(cp);
    if (it == glyphs_.end()) it = glyphs_.find('?');
    static const Glyph none{};
    return it != glyphs_.end() ? it->second : none;
}

float SdfFont::kerning(uint32_t a, uint32_t b) const {
    if (!info_) return 0.0f;
    const uint64_t key = (static_cast<uint64_t>(a) << 32) | b;
    const auto it = kernCache_.find(key);
    if (it != kernCache_.end()) return it->second;
    const float k = stbtt_GetCodepointKernAdvance(&info_->font, static_cast<int>(a), static_cast<int>(b)) * scale_;
    kernCache_.emplace(key, k);
    return k;
}

float SdfFont::width(const std::string& text, float size) const {
    const std::vector<uint32_t> cps = utf8Codepoints(text);
    float w = 0.0f;
    for (size_t i = 0; i < cps.size(); ++i) {
        w += glyph(cps[i]).advance;
        if (i + 1 < cps.size()) w += kerning(cps[i], cps[i + 1]);
    }
    return w * size / kBakeSize;
}

std::vector<std::string> SdfFont::wrap(const std::string& text, float size, float maxWidth) const {
    std::vector<std::string> lines;
    std::string line;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(' ', start);
        const std::string word = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!word.empty()) {
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && width(candidate, size) > maxWidth) {
                lines.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
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
        const json::Value* path = root.find(key);
        if (!path || !path->isString()) {
            if (error) *error = file + ": missing \"" + key + "\" (a font file under assets/)";
            return false;
        }
        if (!font->loadFile(assetDir + "/" + path->asString(), error)) return false;
    }
    return true;
}

}  // namespace mythos
