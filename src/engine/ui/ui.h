#ifndef MYTHOS_ENGINE_UI_UI_H_
#define MYTHOS_ENGINE_UI_UI_H_
// A small 2D overlay toolkit: pixel boxes on a Surface, styled text runs in a
// TextLayer, and clickable hotspots. The overlay takes the window's shape at a
// whole-number scale, so pixels stay square. Nothing here knows what is drawn
// with it.

#include <string>
#include <vector>

#include "engine/gfx/surface.h"
#include "engine/gfx/text.h"

namespace mythos {

// The overlay size for a windowW x windowH drawable: the window divided by the
// largest whole scale that still leaves at least minW x minH.
void OverlaySize(int window_w, int window_h, int min_w, int min_h, int& w,
                 int& h);

// A clickable area of the overlay, in its pixels: a row (item) and, for
// sliders, the level it stands for (value; -1 otherwise).
struct Hotspot {
  int item = -1;
  int value = -1;
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// The hotspot under (x, y): the last one listed wins, so a pip beats its row.
// Returns its item (-1 for none) and stores its value in *value.
int HitTest(const std::vector<Hotspot>& hotspots, float x, float y,
            int* value = nullptr);

void FillRect(Surface& s, int x0, int y0, int x1, int y1, uint32_t color);
void DrawBox(Surface& s, int x0, int y0, int x1, int y1, uint32_t fill,
             uint32_t border);

// The colour with its alpha scaled by k.
uint32_t WithAlpha(uint32_t c, double k);

// A font, a size (line height in overlay pixels), a colour and its decoration.
struct Style {
  const SdfFont* font;
  float size;
  uint32_t color;
  TextEffect fx;

  Style Recolored(uint32_t c) const {
    Style s = *this;
    s.color = c;
    return s;
  }
  Style Faded(double k) const {  // the whole style at a fraction of its opacity
    Style s = *this;
    s.color = WithAlpha(color, k);
    s.fx.shadow_color = WithAlpha(fx.shadow_color, k);
    s.fx.outline_color = WithAlpha(fx.outline_color, k);
    return s;
  }
  float LineHeight() const { return size * 0.98f; }
};

TextEffect DropShadow(float blur = 1.2f);
TextEffect Glow(uint32_t color, float blur, float outline = 0.0f);
// The selected menu item's warm, slowly breathing glow.
TextEffect SelectionGlow(double time_sec, float blur);

enum class Align { kLeft, kCenter, kRight };

// The overlay's two layers: pixels (boxes, bars) and text drawn over them.
struct UiCanvas {
  Surface& fb;
  TextLayer& layer;
  std::vector<Hotspot>* hotspots = nullptr;

  void Hot(int item, float x0, float y0, float x1, float y1,
           int value = -1) const {
    if (hotspots) {
      hotspots->push_back({.item = item,
                           .value = value,
                           .x0 = x0,
                           .y0 = y0,
                           .x1 = x1,
                           .y1 = y1});
    }
  }

  static float Width(const Style& s, const std::string& str) {
    return s.font->Width(str, s.size);
  }

  // Writes one line with its box's top at y; returns its width.
  float Text(const Style& s, float x, float y, const std::string& str,
             Align a = Align::kLeft) {
    const float w = Width(s, str);
    const float x0 =
        a == Align::kLeft ? x : (a == Align::kCenter ? x - w / 2 : x - w);
    layer.runs.push_back({.font = s.font,
                          .text = str,
                          .x = x0,
                          .y = y,
                          .size = s.size,
                          .color = s.color,
                          .fx = s.fx});
    return w;
  }

  // Word-wraps a paragraph into maxWidth and writes it; returns the y below the
  // last line.
  float Paragraph(const Style& s, float x, float y, float max_width,
                  const std::string& str) {
    for (const auto& line : s.font->Wrap(str, s.size, max_width)) {
      Text(s, x, y, line);
      y += s.LineHeight();
    }
    return y;
  }
  static int LineCount(const Style& s, float max_width,
                       const std::string& str) {
    return static_cast<int>(s.font->Wrap(str, s.size, max_width).size());
  }
};

}  // namespace mythos

#endif  // MYTHOS_ENGINE_UI_UI_H_
