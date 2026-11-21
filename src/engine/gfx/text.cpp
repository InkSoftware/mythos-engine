#include "engine/gfx/text.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "engine/core/vfs.h"
#include "stb_truetype.h"

namespace mythos {
// stb_truetype's font state plus the file bytes it points into, which must stay
// alive and unmoved.
struct SdfFont::Info {
  std::vector<unsigned char> data;
  stbtt_fontinfo font{};
};

namespace {
// The codepoints a font bakes: ASCII, Latin-1, Greek (with tonos), general
// punctuation (dashes, quotes, bullets, ellipsis) and the angle quotes. Missing
// ones are skipped.
std::vector<uint32_t> BakeSet() {
  std::vector<uint32_t> cps;
  for (uint32_t c = 0x20; c <= 0x7E; ++c) cps.push_back(c);
  for (uint32_t c = 0xA0; c <= 0xFF; ++c) cps.push_back(c);
  for (uint32_t c = 0x384; c <= 0x3CE; ++c) cps.push_back(c);
  for (uint32_t c = 0x2010; c <= 0x2026; ++c) cps.push_back(c);
  cps.push_back(0x2039);
  cps.push_back(0x203A);
  return cps;
}

// The atlas is this wide and as tall as the packed rows need, with a pixel of
// padding between glyphs. The SDF is stored as bytes with 128 on the outline;
// kSdfByteRange is the distance (in bytes) the field spans on each side of
// that.
constexpr int kAtlasWidth = 1024;
constexpr int kAtlasPadding = 1;
constexpr unsigned char kSdfOnEdge = 128;
constexpr float kSdfByteRange = 128.0f;

// A glyph and its distance-field pixels before packing.
struct BakedGlyph {
  uint32_t codepoint;
  SdfFont::Glyph glyph;
  std::vector<uint8_t> pixels;
};

// Renders each codepoint of BakeSet that the font has to a distance field at
// kBakeSize. Glyphs with no outline (spaces) keep metrics but no pixels.
std::vector<BakedGlyph> BakeGlyphs(const stbtt_fontinfo* const font,
                                   const float scale) {
  std::vector<BakedGlyph> baked;
  // Bytes per pixel of distance, so that kSdfReach pixels span the full byte
  // range.
  const float distance_scale = kSdfByteRange / SdfFont::kSdfReach;
  for (const uint32_t cp : BakeSet()) {
    if (!stbtt_FindGlyphIndex(font, static_cast<int>(cp))) continue;
    BakedGlyph entry{.codepoint = cp, .glyph = {}, .pixels = {}};
    int advance = 0, left_bearing = 0;
    stbtt_GetCodepointHMetrics(font, static_cast<int>(cp), &advance,
                               &left_bearing);
    entry.glyph.advance = static_cast<float>(advance) * scale;
    int width = 0, height = 0, x_offset = 0, y_offset = 0;
    unsigned char* sdf = stbtt_GetCodepointSDF(
        font, scale, static_cast<int>(cp), SdfFont::kSdfReach, kSdfOnEdge,
        distance_scale, &width, &height, &x_offset, &y_offset);
    if (sdf) {
      entry.glyph.aw = width;
      entry.glyph.ah = height;
      entry.glyph.x0 = static_cast<float>(x_offset);
      entry.glyph.y0 = static_cast<float>(y_offset);
      entry.pixels.assign(sdf, sdf + static_cast<size_t>(width) * height);
      stbtt_FreeSDF(sdf, nullptr);
    }
    baked.push_back(std::move(entry));
  }
  return baked;
}

// Assigns atlas positions by simple left-to-right row packing and returns the
// atlas height needed.
int PackAtlasRows(std::vector<BakedGlyph>& baked) {
  int pen_x = kAtlasPadding, pen_y = kAtlasPadding, row_height = 0;
  for (auto& entry : baked) {
    if (entry.glyph.aw == 0) continue;
    if (pen_x + entry.glyph.aw + kAtlasPadding > kAtlasWidth) {
      pen_x = kAtlasPadding;
      pen_y += row_height + kAtlasPadding;
      row_height = 0;
    }
    entry.glyph.ax = pen_x;
    entry.glyph.ay = pen_y;
    pen_x += entry.glyph.aw + kAtlasPadding;
    row_height = std::max(row_height, entry.glyph.ah);
  }
  return pen_y + row_height + kAtlasPadding;
}
}

// A lead byte whose continuation bytes are missing, or a stray continuation
// byte, becomes '?' and consumes one byte. Continuation bytes themselves are
// not validated.
std::vector<uint32_t> Utf8Codepoints(const std::string& text) {
  std::vector<uint32_t> out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    const auto b0 = static_cast<unsigned char>(text[i]);
    const auto cont = [&](const size_t k) {
      return static_cast<uint32_t>(static_cast<unsigned char>(text[i + k]) &
                                   0x3F);
    };
    if (b0 < 0x80) {
      out.push_back(b0);
      i += 1;
    } else if ((b0 & 0xE0) == 0xC0 && i + 1 < text.size()) {
      out.push_back((static_cast<uint32_t>(b0 & 0x1F) << 6) | cont(1));
      i += 2;
    } else if ((b0 & 0xF0) == 0xE0 && i + 2 < text.size()) {
      out.push_back((static_cast<uint32_t>(b0 & 0x0F) << 12) | (cont(1) << 6) |
                    cont(2));
      i += 3;
    } else if ((b0 & 0xF8) == 0xF0 && i + 3 < text.size()) {
      out.push_back((static_cast<uint32_t>(b0 & 0x07) << 18) | (cont(1) << 12) |
                    (cont(2) << 6) | cont(3));
      i += 4;
    } else {
      out.push_back('?');
      i += 1;
    }
  }
  return out;
}

SdfFont::SdfFont(SdfFont&& other) noexcept { *this = std::move(other); }

// latin_ points into glyphs_, whose nodes survive the move of the map, so the
// table is copied as is.
SdfFont& SdfFont::operator=(SdfFont&& other) noexcept {
  if (this == &other) return *this;
  delete info_;
  info_ = std::exchange(other.info_, nullptr);
  scale_ = other.scale_;
  ascent_ = other.ascent_;
  glyphs_ = std::move(other.glyphs_);
  kern_cache_ = std::move(other.kern_cache_);
  latin_ = other.latin_;
  kern_ascii_ = std::move(other.kern_ascii_);
  atlas_w_ = other.atlas_w_;
  atlas_h_ = other.atlas_h_;
  atlas_ = std::move(other.atlas_);
  return *this;
}

SdfFont::~SdfFont() { delete info_; }

bool SdfFont::LoadFile(const std::string& path, std::string* const error) {
  const auto fail = [&](const std::string& why) {
    if (error) *error = path + ": " + why;
    return false;
  };
  std::string bytes;
  if (!vfs::ReadFile(path, bytes)) return fail("cannot open the font");
  // Parse into a fresh Info first, so a bad file leaves the current font alone.
  auto info = new Info;
  info->data.assign(bytes.begin(), bytes.end());
  const int offset = stbtt_GetFontOffsetForIndex(info->data.data(), 0);
  if (info->data.empty() || offset < 0 ||
      !stbtt_InitFont(&info->font, info->data.data(), offset)) {
    delete info;
    return fail("not a TrueType/OpenType font");
  }
  delete info_;
  info_ = info;
  glyphs_.clear();
  kern_cache_.clear();
  kern_ascii_.clear();
  latin_.fill(nullptr);

  const stbtt_fontinfo* fi = &info_->font;
  scale_ = stbtt_ScaleForPixelHeight(fi, kBakeSize);
  int asc = 0, desc = 0, gap = 0;
  stbtt_GetFontVMetrics(fi, &asc, &desc, &gap);
  ascent_ = static_cast<float>(asc) * scale_;

  std::vector<BakedGlyph> baked = BakeGlyphs(fi, scale_);
  if (baked.empty()) return fail("has none of the game's characters");

  atlas_w_ = kAtlasWidth;
  atlas_h_ = PackAtlasRows(baked);
  atlas_.assign(static_cast<size_t>(atlas_w_) * atlas_h_, 0);
  for (auto& entry : baked) {
    const struct Glyph& g = entry.glyph;
    for (int y = 0; y < g.ah; ++y) {
      std::copy_n(entry.pixels.begin() + static_cast<std::ptrdiff_t>(y) * g.aw,
                  g.aw,
                  atlas_.begin() +
                      static_cast<std::ptrdiff_t>(g.ay + y) * atlas_w_ + g.ax);
    }
    glyphs_[entry.codepoint] = g;
  }
  // Latin-1 lookup table: each codepoint maps to its glyph, else '?', else an
  // empty glyph.
  static const struct Glyph kNone{};
  const auto fallback = glyphs_.find('?');
  for (uint32_t cp = 0; cp < 256; ++cp) {
    const auto it = glyphs_.find(cp);
    latin_[cp] = it != glyphs_.end()         ? &it->second
                 : fallback != glyphs_.end() ? &fallback->second
                                             : &kNone;
  }
  return true;
}

const SdfFont::Glyph& SdfFont::GlyphFor(const uint32_t cp) const {
  if (cp < 256 && latin_[cp]) return *latin_[cp];
  auto it = glyphs_.find(cp);
  if (it == glyphs_.end()) it = glyphs_.find('?');
  static const struct Glyph kNone{};
  return it != glyphs_.end() ? it->second : kNone;
}

// Kerning is looked up from the font once per pair: ASCII pairs in a flat table
// (NaN = unknown), everything else in a hash map.
float SdfFont::Kerning(const uint32_t a, const uint32_t b) const {
  if (!info_) return 0.0f;
  float* slot = nullptr;
  constexpr uint32_t kAscii = 128;
  if (a < kAscii && b < kAscii) {
    if (kern_ascii_.empty())
      kern_ascii_.assign(static_cast<size_t>(kAscii) * kAscii, std::nanf(""));
    slot = &kern_ascii_[a * kAscii + b];
    if (!std::isnan(*slot)) return *slot;
  }
  const uint64_t key = (static_cast<uint64_t>(a) << 32) | b;
  const auto it = kern_cache_.find(key);
  if (it != kern_cache_.end()) return it->second;
  const float kern =
      static_cast<float>(stbtt_GetCodepointKernAdvance(
          &info_->font, static_cast<int>(a), static_cast<int>(b))) *
      scale_;
  kern_cache_.emplace(key, kern);
  if (slot) *slot = kern;
  return kern;
}

float SdfFont::BakedWidth(const std::vector<uint32_t>& cps) const {
  float width = 0.0f;
  for (size_t i = 0; i < cps.size(); ++i) {
    width += GlyphFor(cps[i]).advance;
    if (i + 1 < cps.size()) width += Kerning(cps[i], cps[i + 1]);
  }
  return width;
}

float SdfFont::Width(const std::string& text, const float size) const {
  return BakedWidth(Utf8Codepoints(text)) * size / kBakeSize;
}

// Greedy fill. A candidate line's width includes the kerning around the joining
// space, as it would be laid out.
std::vector<std::string> SdfFont::Wrap(const std::string& text,
                                       const float size,
                                       const float max_width) const {
  std::vector<std::string> lines;
  std::string line;

  float line_w = 0.0f;
  uint32_t line_last = 0;
  const float space = GlyphFor(' ').advance;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = text.find(' ', start);
    const std::string word = text.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    if (!word.empty()) {
      const std::vector<uint32_t> cps = Utf8Codepoints(word);
      const float word_w = BakedWidth(cps);
      if (line.empty()) {
        line = word;
        line_w = word_w;
      } else {
        const float candidate_w = line_w + Kerning(line_last, ' ') + space +
                                  Kerning(' ', cps.front()) + word_w;
        if (candidate_w * size / kBakeSize > max_width) {
          lines.push_back(line);
          line = word;
          line_w = word_w;
        } else {
          line += ' ';
          line += word;
          line_w = candidate_w;
        }
      }
      line_last = cps.back();
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

bool LoadFontFiles(const json::Value& root, const std::string& file,
                   const std::string& asset_dir,
                   const std::vector<std::pair<const char*, SdfFont*>>& roles,
                   std::string* const error) {
  for (const auto& [key, font] : roles) {
    const json::Value* path = json::Find(root, key);
    if (!path || !path->is_string()) {
      if (error) {
        *error = file + ": missing \"" + key + "\" (a font file under assets/)";
      }
      return false;
    }
    if (!font->LoadFile(asset_dir + "/" + path->get<std::string>(), error)) {
      return false;
    }
  }
  return true;
}
}
