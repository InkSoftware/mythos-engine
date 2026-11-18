#include "engine/ui/ui.h"

#include <cmath>

#include <algorithm>

namespace mythos {

void overlaySize(int windowW, int windowH, int minW, int minH, int& w, int& h) {
    const int scale = std::max(1, std::min(windowW / minW, windowH / minH));
    w = std::max(minW, windowW / scale);
    h = std::max(minH, windowH / scale);
}

int hitTest(const std::vector<Hotspot>& hotspots, float x, float y, int* value) {
    for (auto it = hotspots.rbegin(); it != hotspots.rend(); ++it)
        if (x >= it->x0 && x < it->x1 && y >= it->y0 && y < it->y1) {
            if (value) *value = it->value;
            return it->item;
        }
    if (value) *value = -1;
    return -1;
}

void fillRect(Surface& s, int x0, int y0, int x1, int y1, uint32_t color) {
    for (int y = std::max(0, y0); y < std::min(s.h, y1); ++y)
        for (int x = std::max(0, x0); x < std::min(s.w, x1); ++x) s.at(x, y) = color;
}

void drawBox(Surface& s, int x0, int y0, int x1, int y1, uint32_t fill, uint32_t border) {
    fillRect(s, x0, y0, x1, y1, fill);
    fillRect(s, x0, y0, x1, y0 + 1, border);
    fillRect(s, x0, y1 - 1, x1, y1, border);
    fillRect(s, x0, y0, x0 + 1, y1, border);
    fillRect(s, x1 - 1, y0, x1, y1, border);
}

uint32_t withAlpha(uint32_t color, double factor) {
    const auto alpha = static_cast<uint32_t>(std::clamp(((color >> 24) & 0xFF) * factor, 0.0, 255.0));
    return (alpha << 24) | (color & 0x00FFFFFF);
}

TextEffect dropShadow(float blur) { return {0.0f, 0, 0.5f, 0.7f, blur, 0xE0000000}; }
TextEffect glow(uint32_t color, float blur, float outline) { return {outline, 0xFF0A0404, 0.0f, 0.0f, blur, color}; }

TextEffect selectionGlow(double timeSec, float blur) {
    return glow(withAlpha(0xFFB06A10, 0.55 + 0.25 * std::sin(timeSec * 4.0)), blur);
}

}  // namespace mythos
