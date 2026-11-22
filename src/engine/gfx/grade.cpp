#include "engine/gfx/grade.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mythos {
namespace {
constexpr float kGamma = 2.2f;

float Finite(const float v, const float lo, const float hi,
             const float fallback) {
  return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}

float Hable(const float x) {
  constexpr float a = 0.15f, b = 0.50f, c = 0.10f, d = 0.20f, e = 0.02f,
                  f = 0.30f;
  return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - e / f;
}

float Luma(const float c[3]) {
  return c[0] * 0.299f + c[1] * 0.587f + c[2] * 0.114f;
}

uint32_t Channel(const uint32_t px, const int shift) {
  return (px >> shift) & 0xFFu;
}

// The strip's texel as 0..1 floats.
std::array<float, 3> Texel(const Surface& s, const int size, const int r,
                           const int g, const int b) {
  const uint32_t px = s.px[static_cast<size_t>(g) * static_cast<size_t>(s.w) +
                           static_cast<size_t>(b * size + r)];
  return {static_cast<float>(Channel(px, 16)) / 255.0f,
          static_cast<float>(Channel(px, 8)) / 255.0f,
          static_cast<float>(Channel(px, 0)) / 255.0f};
}
}  // namespace

bool ColorGrade::PostIsIdentity() const {
  return contrast == 1.0f && saturation == 1.0f && tint[0] == 1.0f &&
         tint[1] == 1.0f && tint[2] == 1.0f;
}

ColorGrade Sanitize(const ColorGrade& g) {
  ColorGrade out;
  const int t = static_cast<int>(g.tonemap);
  out.tonemap = t >= 0 && t < kTonemapCount ? g.tonemap : Tonemap::kAces;
  out.exposure = Finite(g.exposure, 0.0f, 16.0f, 1.0f);
  out.contrast = Finite(g.contrast, 0.0f, 4.0f, 1.0f);
  out.saturation = Finite(g.saturation, 0.0f, 4.0f, 1.0f);
  for (int i = 0; i < 3; ++i) out.tint[i] = Finite(g.tint[i], 0.0f, 4.0f, 1.0f);
  out.vignette = Finite(g.vignette, 0.0f, 2.0f, 1.0f);
  out.lut_amount = Finite(g.lut_amount, 0.0f, 1.0f, 1.0f);
  return out;
}

float TonemapChannel(const Tonemap t, const float linear) {
  const float x = std::max(0.0f, linear);
  float v = x;
  switch (t) {
    case Tonemap::kAces:
      v = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
      break;
    case Tonemap::kFilmic:
      v = Hable(x * 2.0f) / Hable(11.2f);
      break;
    case Tonemap::kNone:
      break;
  }
  return std::pow(std::clamp(v, 0.0f, 1.0f), 1.0f / kGamma);
}

std::array<float, 3> ToDisplay(const ColorGrade& g, const float linear[3]) {
  return {TonemapChannel(g.tonemap, linear[0] * g.exposure),
          TonemapChannel(g.tonemap, linear[1] * g.exposure),
          TonemapChannel(g.tonemap, linear[2] * g.exposure)};
}

std::array<float, 3> GradeDisplay(const ColorGrade& g, const float display[3]) {
  std::array<float, 3> c = {(display[0] - 0.5f) * g.contrast + 0.5f,
                            (display[1] - 0.5f) * g.contrast + 0.5f,
                            (display[2] - 0.5f) * g.contrast + 0.5f};
  const float y = Luma(c.data());
  for (int i = 0; i < 3; ++i) {
    c[i] = (y + (c[i] - y) * g.saturation) * g.tint[i];
  }
  return c;
}

Surface IdentityLutStrip(const int size) {
  if (size < kLutMinSize || size > kLutMaxSize) return {};
  Surface s(size * size, size);
  const float scale = 255.0f / static_cast<float>(size - 1);
  for (int g = 0; g < size; ++g) {
    for (int b = 0; b < size; ++b) {
      for (int r = 0; r < size; ++r) {
        s.px[static_cast<size_t>(g) * static_cast<size_t>(s.w) +
             static_cast<size_t>(b * size + r)] =
            Rgba(static_cast<int>(std::lround(static_cast<float>(r) * scale)),
                 static_cast<int>(std::lround(static_cast<float>(g) * scale)),
                 static_cast<int>(std::lround(static_cast<float>(b) * scale)));
      }
    }
  }
  return s;
}

int LutStripSize(const Surface& strip) {
  const int size = strip.h;
  if (size < kLutMinSize || size > kLutMaxSize) return 0;
  if (strip.w != size * size) return 0;
  if (strip.px.size() != static_cast<size_t>(strip.w) * strip.h) return 0;
  return size;
}

std::array<float, 3> ApplyLut(const Surface& strip, const float rgb[3]) {
  const int size = LutStripSize(strip);
  if (size == 0) return {rgb[0], rgb[1], rgb[2]};
  const auto top = static_cast<float>(size - 1);
  float pos[3];
  int lo[3], hi[3];
  float frac[3];
  for (int i = 0; i < 3; ++i) {
    pos[i] =
        std::clamp(std::isfinite(rgb[i]) ? rgb[i] : 0.0f, 0.0f, 1.0f) * top;
    lo[i] = std::min(static_cast<int>(pos[i]), size - 1);
    hi[i] = std::min(lo[i] + 1, size - 1);
    frac[i] = pos[i] - static_cast<float>(lo[i]);
  }
  std::array<float, 3> out = {0, 0, 0};
  for (int corner = 0; corner < 8; ++corner) {
    const bool rh = (corner & 1) != 0, gh = (corner & 2) != 0,
               bh = (corner & 4) != 0;
    const float w = (rh ? frac[0] : 1.0f - frac[0]) *
                    (gh ? frac[1] : 1.0f - frac[1]) *
                    (bh ? frac[2] : 1.0f - frac[2]);
    if (w == 0.0f) continue;
    const std::array<float, 3> t =
        Texel(strip, size, rh ? hi[0] : lo[0], gh ? hi[1] : lo[1],
              bh ? hi[2] : lo[2]);
    for (int i = 0; i < 3; ++i) out[i] += t[i] * w;
  }
  return out;
}
}  // namespace mythos
