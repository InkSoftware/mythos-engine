#include "engine/platform/gl/text_renderer.h"

#include <algorithm>

namespace mythos {
namespace {

using namespace gl;

// Text from a distance-field atlas: 0.5 is the outline of a letter. vParams.x widens the
// letter by an outline ring (in field units), vParams.y blurs it outwards (shadows, glows).
const char* kTextVS = R"GLSL(
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

const char* kTextFS = R"GLSL(
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

// Limits of the distance-field effects, in field units (0.5 is the full reach of the field).
constexpr float kMaxOutlineField = 0.45f, kMaxBlurField = 0.5f;
constexpr int kVertsPerGlyph = 6;  // two triangles
constexpr int kAttribCount = 5;

// 0xAARRGGBB to four floats, r g b a.
void unpackColor(uint32_t argb, float* rgba) {
    rgba[0] = ((argb >> 16) & 0xFF) / 255.0f;
    rgba[1] = ((argb >> 8) & 0xFF) / 255.0f;
    rgba[2] = (argb & 0xFF) / 255.0f;
    rgba[3] = ((argb >> 24) & 0xFF) / 255.0f;
}

}  // namespace

bool TextRenderer::init() {
    if (!program_.build(kTextVS, kTextFS, {{0, "aPos"}, {2, "aUV"}, {3, "aColor"}, {4, "aOutline"}, {5, "aParams"}}))
        return false;
    vbo_.create();
    vao_.create();
    vao_.bind();
    BindBuffer(ARRAY_BUFFER, vbo_.id());
    const GLsizei stride = kFloats * sizeof(float);
    // pos, uv, colour, outline colour, params: floats per attribute and its shader location
    const GLuint sizes[kAttribCount] = {2, 2, 4, 4, 2}, locations[kAttribCount] = {0, 2, 3, 4, 5};
    size_t offset = 0;
    for (int a = 0; a < kAttribCount; ++a) {
        EnableVertexAttribArray(locations[a]);
        VertexAttribPointer(locations[a], static_cast<GLint>(sizes[a]), FLOAT, FALSE_, stride,
                            reinterpret_cast<const void*>(offset * sizeof(float)));
        offset += sizes[a];
    }
    BindVertexArray(0);
    return true;
}

void TextRenderer::addFont(const SdfFont& font) {
    GlTexture tex;
    tex.create();
    tex.bind();
    PixelStorei(UNPACK_ALIGNMENT, 1);
    TexImage2D(TEXTURE_2D, 0, R8, font.atlasWidth(), font.atlasHeight(), 0, RED, UNSIGNED_BYTE, font.atlas().data());
    PixelStorei(UNPACK_ALIGNMENT, 4);
    GenerateMipmap(TEXTURE_2D);
    tex.setParams(LINEAR_MIPMAP_LINEAR, LINEAR, CLAMP_TO_EDGE);
    fonts_.emplace_back(&font, std::move(tex));
    built_ = false;
}

// One pass over a run (the shadow, then the letters themselves): appends two triangles per glyph.
void TextRenderer::appendPass(const TextRun& run, const std::vector<uint32_t>& cps, const SdfFont& font, float sx,
                              float sy, float dx, float dy, uint32_t color, uint32_t outlineColor, float outline,
                              float blur) {
    const float k = run.size / SdfFont::kBakeSize;  // bake pixels -> overlay pixels (the glyph scale)
    // Overlay pixels -> distance-field units (0.5 = the full reach of the field).
    const float toField = 1.0f / (k * 2.0f * SdfFont::kSdfReach);
    const float iw = 1.0f / font.atlasWidth(), ih = 1.0f / font.atlasHeight();
    float col[4], ocol[4];
    unpackColor(color, col);
    unpackColor(outlineColor, ocol);
    const float params[2] = {std::min(outline * toField, kMaxOutlineField), std::min(blur * toField, kMaxBlurField)};
    float penX = run.x + dx;
    const float baseline = run.y + dy + font.ascent(run.size);
    // Written straight into the vector: room for every glyph, trimmed to what was drawn.
    const size_t base = verts_.size();
    verts_.resize(base + cps.size() * kVertsPerGlyph * kFloats);
    float* out = verts_.data() + base;
    for (size_t i = 0; i < cps.size(); ++i) {
        const SdfFont::Glyph& g = font.glyph(cps[i]);
        if (g.aw > 0) {
            const float x0 = penX + g.x0 * k, y0 = baseline + g.y0 * k;
            const float x1 = x0 + g.aw * k, y1 = y0 + g.ah * k;
            const float u0 = g.ax * iw, v0 = g.ay * ih, u1 = (g.ax + g.aw) * iw, v1 = (g.ay + g.ah) * ih;
            const float corners[kVertsPerGlyph][4] = {{x0, y0, u0, v0}, {x1, y0, u1, v0}, {x1, y1, u1, v1},
                                         {x0, y0, u0, v0}, {x1, y1, u1, v1}, {x0, y1, u0, v1}};
            for (const auto& c : corners) {
                const float v[kFloats] = {c[0] * sx - 1.0f, 1.0f - c[1] * sy, c[2], c[3],
                                          col[0], col[1], col[2], col[3],
                                          ocol[0], ocol[1], ocol[2], ocol[3], params[0], params[1]};
                std::copy(v, v + kFloats, out);
                out += kFloats;
            }
        }
        penX += g.advance * k;
        if (i + 1 < cps.size()) penX += font.kerning(cps[i], cps[i + 1]) * k;
    }
    verts_.resize(static_cast<size_t>(out - verts_.data()));
}

void TextRenderer::appendRun(const TextRun& run, const SdfFont& font, float sx, float sy) {
    const TextEffect& fx = run.fx;
    const std::vector<uint32_t> cps = utf8Codepoints(run.text);  // decoded once for both passes
    if (fx.shadowBlur > 0.0f && (fx.shadowColor >> 24) != 0)
        appendPass(run, cps, font, sx, sy, fx.shadowX, fx.shadowY, fx.shadowColor, 0, 0.0f, fx.shadowBlur);
    appendPass(run, cps, font, sx, sy, 0.0f, 0.0f, run.color, fx.outlineColor, fx.outline, 0.0f);
}

// Lays out every run (grouped by font, so each font is one draw) into verts_ and uploads it.
void TextRenderer::rebuildVertices(const TextLayer& text, int overlayW, int overlayH) {
    const float sx = 2.0f / overlayW, sy = 2.0f / overlayH;  // overlay pixels -> clip space
    verts_.clear();
    ranges_.assign(fonts_.size(), FontRange{});
    for (size_t fontIndex = 0; fontIndex < fonts_.size(); ++fontIndex) {
        const SdfFont* font = fonts_[fontIndex].first;
        const size_t before = verts_.size();
        for (const TextRun& run : text.runs)
            if (run.font == font && !run.text.empty()) appendRun(run, *font, sx, sy);
        ranges_[fontIndex] = {static_cast<int>(before / kFloats), static_cast<int>((verts_.size() - before) / kFloats)};
    }
    vbo_.upload(verts_.data(), verts_.size() * sizeof(float), DYNAMIC_DRAW);
    builtRuns_ = text.runs;
    builtW_ = overlayW;
    builtH_ = overlayH;
    built_ = true;
}

void TextRenderer::draw(const TextLayer& text, int overlayW, int overlayH) {
    if (!program_.valid() || text.runs.empty()) return;
    program_.use();
    program_.set("uAtlas", 0);
    ActiveTexture(TEXTURE0);
    vao_.bind();
    if (!built_ || overlayW != builtW_ || overlayH != builtH_ || text.runs != builtRuns_) rebuildVertices(text, overlayW, overlayH);
    Enable(BLEND);
    BlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
    for (size_t fontIndex = 0; fontIndex < fonts_.size(); ++fontIndex) {
        if (ranges_[fontIndex].count == 0) continue;
        fonts_[fontIndex].second.bind();
        DrawArrays(TRIANGLES, ranges_[fontIndex].first, ranges_[fontIndex].count);
    }
    Disable(BLEND);
}

}  // namespace mythos
