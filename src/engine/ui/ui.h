#pragma once
// A small 2D overlay toolkit: pixel boxes on a Surface, styled text runs in a TextLayer, and
// clickable hotspots. The overlay takes the window's shape at a whole-number scale, so
// pixels stay square. Nothing here knows what is drawn with it.

#include <string>
#include <vector>

#include "engine/gfx/surface.h"
#include "engine/gfx/text.h"

namespace mythos {

// The overlay size for a windowW x windowH drawable: the window divided by the largest
// whole scale that still leaves at least minW x minH.
void overlaySize(int windowW, int windowH, int minW, int minH, int& w, int& h);

// A clickable area of the overlay, in its pixels: a row (item) and, for sliders, the level
// it stands for (value; -1 otherwise).
struct Hotspot {
    int item = -1;
    int value = -1;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// The hotspot under (x, y): the last one listed wins, so a pip beats its row. Returns its
// item (-1 for none) and stores its value in *value.
int hitTest(const std::vector<Hotspot>& hotspots, float x, float y, int* value = nullptr);

void fillRect(Surface& s, int x0, int y0, int x1, int y1, uint32_t color);
void drawBox(Surface& s, int x0, int y0, int x1, int y1, uint32_t fill, uint32_t border);

// The colour with its alpha scaled by k.
uint32_t withAlpha(uint32_t c, double k);

// A font, a size (line height in overlay pixels), a colour and its decoration.
struct Style {
    const SdfFont* font;
    float size;
    uint32_t color;
    TextEffect fx;

    Style recolored(uint32_t c) const {
        Style s = *this;
        s.color = c;
        return s;
    }
    Style faded(double k) const {  // the whole style at a fraction of its opacity
        Style s = *this;
        s.color = withAlpha(color, k);
        s.fx.shadowColor = withAlpha(fx.shadowColor, k);
        s.fx.outlineColor = withAlpha(fx.outlineColor, k);
        return s;
    }
    float lineHeight() const { return size * 0.98f; }
};

TextEffect dropShadow(float blur = 1.2f);
TextEffect glow(uint32_t color, float blur, float outline = 0.0f);
// The selected menu item's warm, slowly breathing glow.
TextEffect selectionGlow(double timeSec, float blur);

enum class Align { Left, Center, Right };

// The overlay's two layers: pixels (boxes, bars) and text drawn over them.
struct UiCanvas {
    Surface& fb;
    TextLayer& layer;
    std::vector<Hotspot>* hotspots = nullptr;

    void hot(int item, float x0, float y0, float x1, float y1, int value = -1) {
        if (hotspots) hotspots->push_back({item, value, x0, y0, x1, y1});
    }

    float width(const Style& s, const std::string& str) const { return s.font->width(str, s.size); }

    // Writes one line with its box's top at y; returns its width.
    float text(const Style& s, float x, float y, const std::string& str, Align a = Align::Left) {
        const float w = width(s, str);
        const float x0 = a == Align::Left ? x : (a == Align::Center ? x - w / 2 : x - w);
        layer.runs.push_back({s.font, str, x0, y, s.size, s.color, s.fx});
        return w;
    }

    // Word-wraps a paragraph into maxWidth and writes it; returns the y below the last line.
    float paragraph(const Style& s, float x, float y, float maxWidth, const std::string& str) {
        for (const auto& line : s.font->wrap(str, s.size, maxWidth)) {
            text(s, x, y, line);
            y += s.lineHeight();
        }
        return y;
    }
    int lineCount(const Style& s, float maxWidth, const std::string& str) const {
        return static_cast<int>(s.font->wrap(str, s.size, maxWidth).size());
    }
};

}  // namespace mythos
