#include "engine/platform/gl/text_renderer.h"

#include <algorithm>

namespace mythos {
namespace {
using gl::ActiveTexture;
using gl::BindBuffer;
using gl::BindVertexArray;
using gl::BlendFunc;
using gl::Disable;
using gl::DrawArrays;
using gl::Enable;
using gl::EnableVertexAttribArray;
using gl::GenerateMipmap;
using gl::GLint;
using gl::GLsizei;
using gl::GLuint;
using gl::kArrayBuffer;
using gl::kBlend;
using gl::kClampToEdge;
using gl::kDynamicDraw;
using gl::kFalse;
using gl::kFloat;
using gl::kLinear;
using gl::kLinearMipmapLinear;
using gl::kOneMinusSrcAlpha;
using gl::kR8;
using gl::kRed;
using gl::kSrcAlpha;
using gl::kTexture0;
using gl::kTexture2D;
using gl::kTriangles;
using gl::kUnpackAlignment;
using gl::kUnsignedByte;
using gl::PixelStorei;
using gl::TexImage2D;
using gl::VertexAttribPointer;

const char* k_text_vs = R"GLSL(
#version 410 core
in vec2 aPos;
in vec2 aUV;
in vec4 aColor;
in vec4 aOutline;
in vec2 aParams;
out vec2 vUV;
out vec4 vColor;
out vec4 vOutline;
out vec2 vParams;
void main() {
    vUV = aUV;
    vColor = aColor;
    vOutline = aOutline;
    vParams = aParams;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

// Field value 0.5 is the glyph edge. A blur pass fades out softly (squared for
// a smoother falloff); otherwise the edge is anti-aliased by the field's
// screen-space derivative, with an outline band growing outward from it.
const char* k_text_fs = R"GLSL(
#version 410 core
in vec2 vUV;
in vec4 vColor;
in vec4 vOutline;
in vec2 vParams;
uniform sampler2D uAtlas;
out vec4 outColor;
void main() {
    float d = texture(uAtlas, vUV).r;
    float aa = max(fwidth(d) * 0.7, 1e-4);
    float blur = vParams.y;
    if (blur > 0.0) {
        float a = smoothstep(0.5 - aa - blur, 0.5 + aa, d);
        outColor = vec4(vColor.rgb, vColor.a * a * a);
        return;
    }
    float inner = smoothstep(0.5 - aa, 0.5 + aa, d);
    if (vParams.x > 0.0) {
        float edge = 0.5 - vParams.x;
        float outer = smoothstep(edge - aa, edge + aa, d);
        outColor = vec4(mix(vOutline.rgb, vColor.rgb, inner), outer * mix(vOutline.a, vColor.a, inner));
    } else {
        outColor = vec4(vColor.rgb, vColor.a * inner);
    }
}
)GLSL";

// Limits in distance-field units, so the outline and blur stay inside the
// atlas's padding.
constexpr float kMaxOutlineField = 0.45f, kMaxBlurField = 0.5f;
constexpr int kVertsPerGlyph = 6;
constexpr int kAttribCount = 5;

// Splits 0xAARRGGBB into floats in 0..1.
void UnpackColor(const uint32_t argb, float* const rgba) {
  rgba[0] = ((argb >> 16) & 0xFF) / 255.0f;
  rgba[1] = ((argb >> 8) & 0xFF) / 255.0f;
  rgba[2] = (argb & 0xFF) / 255.0f;
  rgba[3] = ((argb >> 24) & 0xFF) / 255.0f;
}
}

bool TextRenderer::Init() {
  if (!program_.Build(k_text_vs, k_text_fs,
                      {{0, "aPos"},
                       {2, "aUV"},
                       {3, "aColor"},
                       {4, "aOutline"},
                       {5, "aParams"}})) {
    return false;
  }
  vbo_.Create();
  vao_.Create();
  vao_.Bind();
  BindBuffer(kArrayBuffer, vbo_.Id());
  const GLsizei stride = kFloats * sizeof(float);

  // Locations 0, 2..5; the layout matches the vertex shader above.
  const GLuint sizes[kAttribCount] = {2, 2, 4, 4, 2},
               locations[kAttribCount] = {0, 2, 3, 4, 5};
  size_t offset = 0;
  for (int a = 0; a < kAttribCount; ++a) {
    EnableVertexAttribArray(locations[a]);
    VertexAttribPointer(locations[a], static_cast<GLint>(sizes[a]), kFloat,
                        kFalse, stride,
                        reinterpret_cast<const void*>(offset * sizeof(float)));
    offset += sizes[a];
  }
  BindVertexArray(0);
  return true;
}

void TextRenderer::AddFont(const SdfFont& font) {
  GlTexture tex;
  tex.Create();
  tex.Bind();
  PixelStorei(kUnpackAlignment, 1);
  TexImage2D(kTexture2D, 0, kR8, font.AtlasWidth(), font.AtlasHeight(), 0, kRed,
             kUnsignedByte, font.Atlas().data());
  PixelStorei(kUnpackAlignment, 4);
  GenerateMipmap(kTexture2D);
  tex.SetParams(kLinearMipmapLinear, kLinear, kClampToEdge);
  fonts_.emplace_back(&font, std::move(tex));
  built_ = false;
}

void TextRenderer::AppendPass(const TextRun& run,
                              const std::vector<uint32_t>& cps,
                              const SdfFont& font, const float sx,
                              const float sy, const float dx, const float dy,
                              const uint32_t color,
                              const uint32_t outline_color, const float outline,
                              const float blur) {
  // Scale from the baked glyph size to the run's pixel size.
  const float k = run.size / SdfFont::kBakeSize;

  // Converts overlay pixels to distance-field units.
  const float to_field = 1.0f / (k * 2.0f * SdfFont::kSdfReach);
  const float iw = 1.0f / font.AtlasWidth(), ih = 1.0f / font.AtlasHeight();
  float col[4], ocol[4];
  UnpackColor(color, col);
  UnpackColor(outline_color, ocol);
  const float params[2] = {std::min(outline * to_field, kMaxOutlineField),
                           std::min(blur * to_field, kMaxBlurField)};
  float pen_x = run.x + dx;
  const float baseline = run.y + dy + font.Ascent(run.size);

  const size_t base = verts_.size();
  verts_.resize(base + cps.size() * kVertsPerGlyph * kFloats);
  float* out = verts_.data() + base;
  for (size_t i = 0; i < cps.size(); ++i) {
    const SdfFont::Glyph& g = font.GlyphFor(cps[i]);
    if (g.aw > 0) {
      const float x0 = pen_x + g.x0 * k, y0 = baseline + g.y0 * k;
      const float x1 = x0 + g.aw * k, y1 = y0 + g.ah * k;
      const float u0 = g.ax * iw, v0 = g.ay * ih, u1 = (g.ax + g.aw) * iw,
                  v1 = (g.ay + g.ah) * ih;
      const float corners[kVertsPerGlyph][4] = {
          {x0, y0, u0, v0}, {x1, y0, u1, v0}, {x1, y1, u1, v1},
          {x0, y0, u0, v0}, {x1, y1, u1, v1}, {x0, y1, u0, v1}};
      for (const auto& c : corners) {
        const float v[kFloats] = {
            c[0] * sx - 1.0f, 1.0f - c[1] * sy, c[2],      c[3],     col[0],
            col[1],           col[2],           col[3],    ocol[0],  ocol[1],
            ocol[2],          ocol[3],          params[0], params[1]};
        std::copy_n(v, kFloats, out);
        out += kFloats;
      }
    }
    pen_x += g.advance * k;
    if (i + 1 < cps.size()) pen_x += font.Kerning(cps[i], cps[i + 1]) * k;
  }
  verts_.resize(static_cast<size_t>(out - verts_.data()));
}

void TextRenderer::AppendRun(const TextRun& run, const SdfFont& font,
                             const float sx, const float sy) {
  const TextEffect& fx = run.fx;
  const std::vector<uint32_t> cps = Utf8Codepoints(run.text);
  if (fx.shadow_blur > 0.0f && (fx.shadow_color >> 24) != 0) {
    AppendPass(run, cps, font, sx, sy, fx.shadow_x, fx.shadow_y,
               fx.shadow_color, 0, 0.0f, fx.shadow_blur);
  }
  AppendPass(run, cps, font, sx, sy, 0.0f, 0.0f, run.color, fx.outline_color,
             fx.outline, 0.0f);
}

void TextRenderer::RebuildVertices(const TextLayer& text, const int overlay_w,
                                   const int overlay_h) {
  const float sx = 2.0f / overlay_w, sy = 2.0f / overlay_h;
  verts_.clear();
  ranges_.assign(fonts_.size(), FontRange{});
  for (size_t font_index = 0; font_index < fonts_.size(); ++font_index) {
    const SdfFont* font = fonts_[font_index].first;
    const size_t before = verts_.size();
    for (const TextRun& run : text.runs) {
      if (run.font == font && !run.text.empty()) AppendRun(run, *font, sx, sy);
    }
    ranges_[font_index] = {
        .first = static_cast<int>(before / kFloats),
        .count = static_cast<int>((verts_.size() - before) / kFloats)};
  }
  vbo_.Upload(verts_.data(), verts_.size() * sizeof(float), kDynamicDraw);
  built_runs_ = text.runs;
  built_w_ = overlay_w;
  built_h_ = overlay_h;
  built_ = true;
}

void TextRenderer::Draw(const TextLayer& text, const int overlay_w,
                        const int overlay_h) {
  if (!program_.Valid() || text.runs.empty()) return;
  program_.Use();
  program_.Set("uAtlas", 0);
  ActiveTexture(kTexture0);
  vao_.Bind();
  if (!built_ || overlay_w != built_w_ || overlay_h != built_h_ ||
      text.runs != built_runs_) {
    RebuildVertices(text, overlay_w, overlay_h);
  }
  Enable(kBlend);
  BlendFunc(kSrcAlpha, kOneMinusSrcAlpha);
  for (size_t font_index = 0; font_index < fonts_.size(); ++font_index) {
    if (ranges_[font_index].count == 0) continue;
    fonts_[font_index].second.Bind();
    DrawArrays(kTriangles, ranges_[font_index].first,
               ranges_[font_index].count);
  }
  Disable(kBlend);
}
}
