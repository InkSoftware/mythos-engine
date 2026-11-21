#ifndef MYTHOS_ENGINE_PLATFORM_GL_TEXT_RENDERER_H_
#define MYTHOS_ENGINE_PLATFORM_GL_TEXT_RENDERER_H_

#include <utility>
#include <vector>

#include "engine/gfx/text.h"
#include "engine/platform/gl/gl_objects.h"

namespace mythos {
// Draws a `TextLayer` of SDF glyph runs over the overlay. Vertices are rebuilt
// only when the runs or the overlay size change. One atlas texture per font,
// with one draw per font. Fonts passed to AddFont must outlive the renderer.
class TextRenderer {
 public:
  // Builds the shader and vertex layout. Returns false on a shader error.
  bool Init();

  // Uploads the font's atlas and registers it; runs refer to the font by
  // address.
  void AddFont(const SdfFont& font);

  // Draws the runs alpha-blended. Run coordinates are in overlay pixels
  // (overlay_w x overlay_h), origin top left.
  void Draw(const TextLayer& text, int overlay_w, int overlay_h);

 private:
  // Floats per vertex: position 2, uv 2, color 4, outline color 4, params 2.
  static constexpr int kFloats = 14;

  void RebuildVertices(const TextLayer& text, int overlay_w, int overlay_h);

  void AppendRun(const TextRun& run, const SdfFont& font, float sx, float sy);

  // Appends the quads of one rendering of a run (the shadow or the face),
  // offset by (dx, dy); sx and sy convert overlay pixels to clip space.
  // `outline` and `blur` are in overlay pixels.
  void AppendPass(const TextRun& run, const std::vector<uint32_t>& cps,
                  const SdfFont& font, float sx, float sy, float dx, float dy,
                  uint32_t color, uint32_t outline_color, float outline,
                  float blur);

  GlProgram program_;
  GlVertexArray vao_;
  GlBuffer vbo_;
  std::vector<std::pair<const SdfFont*, GlTexture>> fonts_;

  struct FontRange {
    int first = 0, count = 0;
  };

  std::vector<float> verts_;
  std::vector<FontRange> ranges_;
  std::vector<TextRun> built_runs_;
  int built_w_ = 0, built_h_ = 0;
  bool built_ = false;
};
}

#endif
