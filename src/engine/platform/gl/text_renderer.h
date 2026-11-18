#ifndef MYTHOS_ENGINE_PLATFORM_GL_TEXT_RENDERER_H_
#define MYTHOS_ENGINE_PLATFORM_GL_TEXT_RENDERER_H_
// Draws a TextLayer from distance-field font atlases (see SdfFont) at the
// window's own resolution. Owned by SceneRenderer, which calls draw() after the
// 2D overlay.

#include <utility>
#include <vector>

#include "engine/gfx/text.h"
#include "engine/platform/gl/gl_objects.h"

namespace mythos {

class TextRenderer {
 public:
  // Builds the shader and buffers. The GL context must be current.
  bool Init();
  // Uploads a font's atlas. The font must outlive the renderer's use of it.
  void AddFont(const SdfFont& font);
  // Draws the runs, laid out in overlay pixels (overlayW x overlayH), over the
  // whole viewport.
  void Draw(const TextLayer& text, int overlay_w, int overlay_h);

 private:
  static constexpr int kFloats =
      14;  // pos 2, uv 2, colour 4, outline colour 4, params 2

  void RebuildVertices(const TextLayer& text, int overlay_w, int overlay_h);
  void AppendRun(const TextRun& run, const SdfFont& font, float sx, float sy);
  void AppendPass(const TextRun& run, const std::vector<uint32_t>& cps,
                  const SdfFont& font, float sx, float sy, float dx, float dy,
                  uint32_t color, uint32_t outline_color, float outline,
                  float blur);

  GlProgram program_;
  GlVertexArray vao_;
  GlBuffer vbo_;
  std::vector<std::pair<const SdfFont*, GlTexture>> fonts_;
  // The vertices of the last layout, one range per font, and what they were
  // built for: an identical layout (the usual frame) is drawn again without
  // being laid out or uploaded.
  struct FontRange {
    int first = 0, count = 0;
  };
  std::vector<float> verts_;
  std::vector<FontRange> ranges_;
  std::vector<TextRun> built_runs_;
  int built_w_ = 0, built_h_ = 0;
  bool built_ = false;
};

}  // namespace mythos

#endif  // MYTHOS_ENGINE_PLATFORM_GL_TEXT_RENDERER_H_
