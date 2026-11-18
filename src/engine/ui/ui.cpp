#include "engine/ui/ui.h"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace mythos {

void OverlaySize(int window_w, int window_h, int min_w, int min_h, int& w,
                 int& h) {
  const int scale = std::max(1, std::min(window_w / min_w, window_h / min_h));
  w = std::max(min_w, window_w / scale);
  h = std::max(min_h, window_h / scale);
}

int HitTest(const std::vector<Hotspot>& hotspots, float x, float y,
            int* value) {
  for (const auto& hotspot : std::views::reverse(hotspots)) {
    if (x >= hotspot.x0 && x < hotspot.x1 && y >= hotspot.y0 &&
        y < hotspot.y1) {
      if (value) *value = hotspot.value;
      return hotspot.item;
    }
  }
  if (value) *value = -1;
  return -1;
}

void FillRect(Surface& s, int x0, int y0, int x1, int y1, uint32_t color) {
  for (int y = std::max(0, y0); y < std::min(s.h, y1); ++y) {
    for (int x = std::max(0, x0); x < std::min(s.w, x1); ++x) {
      s.At(x, y) = color;
    }
  }
}

void DrawBox(Surface& s, int x0, int y0, int x1, int y1, uint32_t fill,
             uint32_t border) {
  FillRect(s, x0, y0, x1, y1, fill);
  FillRect(s, x0, y0, x1, y0 + 1, border);
  FillRect(s, x0, y1 - 1, x1, y1, border);
  FillRect(s, x0, y0, x0 + 1, y1, border);
  FillRect(s, x1 - 1, y0, x1, y1, border);
}

uint32_t WithAlpha(uint32_t color, double factor) {
  const auto alpha = static_cast<uint32_t>(
      std::clamp(((color >> 24) & 0xFF) * factor, 0.0, 255.0));
  return (alpha << 24) | (color & 0x00FFFFFF);
}

TextEffect DropShadow(float blur) {
  return {.outline = 0.0f,
          .outline_color = 0,
          .shadow_x = 0.5f,
          .shadow_y = 0.7f,
          .shadow_blur = blur,
          .shadow_color = 0xE0000000};
}
TextEffect Glow(uint32_t color, float blur, float outline) {
  return {.outline = outline,
          .outline_color = 0xFF0A0404,
          .shadow_x = 0.0f,
          .shadow_y = 0.0f,
          .shadow_blur = blur,
          .shadow_color = color};
}

TextEffect SelectionGlow(double time_sec, float blur) {
  return Glow(WithAlpha(0xFFB06A10, 0.55 + 0.25 * std::sin(time_sec * 4.0)),
              blur);
}

}  // namespace mythos
