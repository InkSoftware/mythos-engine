#ifndef MYTHOS_ENGINE_UI_UI_H_
#define MYTHOS_ENGINE_UI_UI_H_

#include <string>
#include <vector>

#include "engine/gfx/surface.h"
#include "engine/gfx/text.h"

namespace mythos {
// Picks the overlay resolution for a window: the largest integer scale at which
// `min_w` x `min_h` still fits, so overlay pixels stay square and whole.
// Returns the size in `w` and `h`, never below the minimum.
void OverlaySize(int window_w, int window_h, int min_w, int min_h, int& w,
                 int& h);

// A clickable rectangle in overlay pixels, with the menu item it belongs to and
// an optional sub-value (such as a meter pip).
struct Hotspot {
  int item = -1;
  int value = -1;
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// Returns the item of the topmost (last added) hotspot containing (x, y), or
// -1. The hotspot's value is stored in `value` when given (-1 when none hit).
int HitTest(const std::vector<Hotspot>& hotspots, float x, float y,
            int* value = nullptr);

// Fills the half-open rectangle [x0, x1) x [y0, y1), clipped to the surface.
// The color is written as is, with no blending.
void FillRect(Surface& s, int x0, int y0, int x1, int y1, uint32_t color);

// Fills a rectangle and draws a one pixel border inside its edge.
void DrawBox(Surface& s, int x0, int y0, int x1, int y1, uint32_t fill,
             uint32_t border);

// Scales the alpha byte of a color by `k` (clamped to 0..255).
uint32_t WithAlpha(uint32_t c, double k);

// Font, size, color and effect of a piece of text. The helpers return modified
// copies.
struct Style {
  const SdfFont* font;
  float size;
  uint32_t color;
  TextEffect fx;

  [[nodiscard]] Style Recolored(const uint32_t c) const {
    Style s = *this;
    s.color = c;
    return s;
  }

  [[nodiscard]] Style Faded(const double k) const {
    Style s = *this;
    s.color = WithAlpha(color, k);
    s.fx.shadow_color = WithAlpha(fx.shadow_color, k);
    s.fx.outline_color = WithAlpha(fx.outline_color, k);
    return s;
  }

  [[nodiscard]] float LineHeight() const { return size * 0.98f; }
};

// Ready-made text effects: a soft dark shadow, a colored glow with an optional
// dark outline, and a pulsing amber glow for the selected menu row.
TextEffect DropShadow(float blur = 1.2f);

TextEffect Glow(uint32_t color, float blur, float outline = 0.0f);

TextEffect SelectionGlow(double time_sec, float blur);

// Horizontal anchor of text relative to its x position.
enum class Align { kLeft, kCenter, kRight };

// What UI code draws on: the overlay surface for boxes, the text layer for
// text, and optionally a list that collects hotspots (null when the caller does
// not need hit testing).
struct UiCanvas {
  Surface& fb;
  TextLayer& layer;
  std::vector<Hotspot>* hotspots = nullptr;

  // Registers a hotspot if the canvas collects them.
  void Hot(const int item, const float x0, const float y0, const float x1,
           const float y1, const int value = -1) const {
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

  // Queues one line of text and returns its width. `x` is the left, center or
  // right edge according to `a`.
  float Text(const Style& s, const float x, const float y,
             const std::string& str, const Align a = Align::kLeft) {
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

  // Queues text wrapped to `max_width` and returns the y below the last line.
  float Paragraph(const Style& s, const float x, float y, const float max_width,
                  const std::string& str) {
    for (const auto& line : s.font->Wrap(str, s.size, max_width)) {
      Text(s, x, y, line);
      y += s.LineHeight();
    }
    return y;
  }

  // Number of lines `Paragraph` would use.
  static int LineCount(const Style& s, const float max_width,
                       const std::string& str) {
    return static_cast<int>(s.font->Wrap(str, s.size, max_width).size());
  }
};
}

#endif
