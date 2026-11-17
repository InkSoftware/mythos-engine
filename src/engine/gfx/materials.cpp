#include "engine/gfx/materials.h"

#include <cmath>
#include <cstdint>

#include "engine/gfx/font.h"
#include "engine/core/noise.h"

namespace mythos {
namespace {

Surface makeBrick() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const int row = y / 8;
            const int off = (row & 1) * 8;
            const int bx = (x + off) % 16;
            const int brick = ((x + off) / 16) + row * 7;
            if (y % 8 == 0 || bx == 0) {  // mortar
                const int v = 92 + grain(x, y, 11, 14);
                s.at(x, y) = rgba(v, v - 4, v - 10);
                continue;
            }
            const double tint = 0.8 + 0.4 * hnoise(brick, 1, 7);
            const int g = grain(x, y, 3, 14);
            s.at(x, y) = rgba(static_cast<int>((118 + 40 * hnoise(brick, 2, 9)) * tint) + g,
                              static_cast<int>((52 + 14 * hnoise(brick, 3, 9)) * tint) + g / 2,
                              static_cast<int>((38 + 10 * hnoise(brick, 4, 9)) * tint) + g / 3);
        }
    return s;
}

Surface makeWallpaper() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            int r = 38, g = 62, b = 52;
            const double motif = std::sin(x * 0.39) * std::sin(y * 0.39);
            if (motif > 0.55) { r = 70; g = 96; b = 78; }
            if (x % 16 < 2) { r = r * 3 / 4; g = g * 3 / 4; b = b * 3 / 4; }  // stripe seam
            if (hnoise(x / 8, y / 8, 3) > 0.85) { r = r * 4 / 5; g = g * 4 / 5; b = b * 4 / 5; }  // water stain
            const int n = grain(x, y, 5, 6);
            s.at(x, y) = rgba(r + n, g + n, b + n);
        }
    return s;
}

Surface makeStone() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            int v = 105 + grain(x, y, 21, 22);
            if (hnoise(x / 2, y / 2, 23) > 0.97) v -= 45;  // crack
            if ((y / 16 + (x / 32) % 2) % 2 == 0 && y % 16 == 0) v -= 25;  // block joints
            s.at(x, y) = rgba(v, v + 2, v + 9);
        }
    return s;
}

Surface makeWood() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const int plank = y / 16;
            const double tint = 0.85 + 0.25 * hnoise(0, plank, 5);
            double v = 0.9 + 0.12 * std::sin(x * 0.31 + plank * 3.0 + std::sin(y * 0.4) * 1.5);
            if (y % 16 == 0) v *= 0.45;  // plank seam
            if ((x + plank * 17) % 21 == 0) v *= 0.8;
            const int n = grain(x, y, 31, 6);
            s.at(x, y) = rgba(static_cast<int>(124 * tint * v) + n, static_cast<int>(82 * tint * v) + n,
                              static_cast<int>(46 * tint * v) + n);
        }
    return s;
}

Surface makeTile() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            if (x % 16 == 0 || y % 16 == 0) {  // grout
                s.at(x, y) = rgba(70, 70, 72);
                continue;
            }
            const bool light = ((x / 16) + (y / 16)) % 2 == 0;
            int v = light ? 218 : 42;
            v += grain(x, y, 41, 8);
            if (hnoise(x / 3, y / 3, 43) > 0.96) v -= 30;  // dirt
            s.at(x, y) = rgba(v, v - 5, light ? v - 20 : v + 4);
        }
    return s;
}

Surface makeCarpet() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const int d = (x + y) % 16;
            const int e = (x - y + 64) % 16;
            bool motif = d < 2 || e < 2;
            int r = motif ? 140 : 108, g = motif ? 40 : 22, b = motif ? 40 : 30;
            const int n = grain(x, y, 51, 10);
            s.at(x, y) = rgba(r + n, g + n / 2, b + n / 2);
        }
    return s;
}

// Night grass fallback for the yard. Normally replaced by assets/grass.bmp.
Surface makeGrass() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const double tuft = hnoise(x, y, 121) * 0.5 + hnoise(x / 3, y / 3, 123) * 0.5;
            const int v = static_cast<int>(40 + 46 * tuft);
            const int n = grain(x, y, 125, 8);
            s.at(x, y) = rgba(v / 2 + n / 2, v + n, v / 3 + n / 3);
        }
    return s;
}

// Pine boughs at night: near-black green, ragged.
Surface makePine() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const double needles = hnoise(x / 2, y, 181) * 0.6 + hnoise(x, y / 3, 183) * 0.4;
            const int v = static_cast<int>(14 + 30 * needles);
            const int n = grain(x, y, 185, 4);
            s.at(x, y) = rgba(v / 2 + n / 2, v + n, v / 2 + n / 2);
        }
    return s;
}

// Mountain rock at night: uneven grey with deep cracks, nothing laid in courses.
Surface makeRock() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const double mass = hnoise(x / 8, y / 8, 191) * 0.5 + hnoise(x / 3, y / 5, 193) * 0.35 +
                                hnoise(x, y, 195) * 0.15;
            int v = static_cast<int>(44 + 60 * mass);
            if (hnoise(x / 2, y / 9, 197) > 0.82) v -= 12;  // a crack
            const int n = grain(x, y, 199, 4);
            s.at(x, y) = rgba(v + n, v + n, v + 3 + n);
        }
    return s;
}

Surface makeDoor() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            int plank = x / 16;
            int v = 96 + grain(plank, 0, 71, 8);
            bool panel = (x % 32 >= 6 && x % 32 < 26 && y >= 8 && y < 28) || (x % 32 >= 6 && x % 32 < 26 && y >= 36 && y < 56);
            if (x % 16 == 0) v -= 25;
            if (panel) v += 14;
            s.at(x, y) = rgba(v, v * 62 / 96, v * 34 / 96);
        }
    // Gold handle.
    for (int y = 29; y < 35; ++y)
        for (int x = 50; x < 56; ++x)
            if ((x - 53) * (x - 53) + (y - 32) * (y - 32) <= 6) s.at(x, y) = rgba(220, 180, 60);
    return s;
}

Surface makeCeiling() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            int v = 48 + grain(x, y, 81, 8);
            if (hnoise(x / 6, y / 6, 77) > 0.8) {  // water stain
                s.at(x, y) = rgba(v + 22, v + 12, v - 4);
                continue;
            }
            s.at(x, y) = rgba(v, v - 4, v - 6);
        }
    return s;
}

// PLACEHOLDER poster: magenta frame with a label. Replace with assets/poster.bmp.
Surface makePosterPlaceholder() {
    Surface s(64, 64, rgba(200, 40, 140));
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (x < 3 || y < 3 || x > 60 || y > 60) s.at(x, y) = rgba(10, 10, 10);
    for (int y = 4; y < 60; ++y)
        for (int x = 4; x < 60; ++x)
            if (((x / 8) + (y / 8)) % 2 == 0) s.at(x, y) = rgba(230, 200, 60);
    drawText(s, 10, 22, "PLACE", rgba(20, 20, 20), 1);
    drawText(s, 10, 32, "HOLDER", rgba(20, 20, 20), 1);
    return s;
}

// PLACEHOLDER painting. Replace with assets/painting.bmp.
Surface makePaintingPlaceholder() {
    Surface s(64, 64, rgba(150, 120, 40));
    for (int y = 4; y < 60; ++y)
        for (int x = 4; x < 60; ++x) s.at(x, y) = (y < 36) ? rgba(90, 120, 150) : rgba(70, 100, 60);
    drawText(s, 12, 14, "NO ART", rgba(240, 240, 220), 1);
    drawText(s, 16, 44, "YET", rgba(240, 240, 220), 1);
    return s;
}

// White enamel: fridges, toilets, sinks, washing machines.
Surface makeEnamel() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const int n = grain(x, y, 111, 4);
            s.at(x, y) = rgba(218 + n, 220 + n, 218 + n);
        }
    return s;
}

// Brushed metal: cookers, handles, tv frames.
Surface makeMetal() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const int streak = static_cast<int>(hnoise(x, 0, 113) * 22);
            const int n = grain(x, y, 115, 5);
            s.at(x, y) = rgba(120 + streak + n, 124 + streak + n, 130 + streak + n);
        }
    return s;
}

// Cream writing paper with a faint laid texture: envelopes and letters.
Surface makePaper() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const int n = grain(x, y, 131, 5) + (y % 4 == 0 ? -4 : 0);
            s.at(x, y) = rgba(226 + n, 214 + n, 186 + n);
        }
    return s;
}

// Dark wool: a man's good suit.
Surface makeCloth() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const int weave = ((x + y) % 4 < 2) ? 3 : -3;  // a faint twill
            const int n = grain(x, y, 137, 4) + weave;
            s.at(x, y) = rgba(30 + n, 32 + n, 40 + n);
        }
    return s;
}

// Wet asphalt at night: near-black, a worn centre line picking up the headlights.
Surface makeAsphalt() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const int n = grain(x, y, 167, 7) + static_cast<int>(hnoise(x / 4, y / 4, 169) * 10);
            int v = 34 + n;
            if (x >= 30 && x < 34 && y % 32 < 18 && hnoise(x, y, 171) > 0.3) v = 120 + n;  // the dashes
            s.at(x, y) = rgba(v, v, v + 3);
        }
    return s;
}

// 1970s wallpaper: big orange and brown diamonds on cream.
Surface makeWallpaper70s() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const int d = std::abs((x % 32) - 16) + std::abs((y % 32) - 16);
            int r = 208, g = 188, b = 150;                     // cream ground
            if (d < 13) { r = 176; g = 108; b = 48; }          // orange diamond
            if (d < 7) { r = 120; g = 74; b = 40; }            // brown core
            if (d == 13 || d == 7) { r = 92; g = 58; b = 34; } // outlines
            const int n = grain(x, y, 157, 5);
            s.at(x, y) = rgba(r + n, g + n, b + n);
        }
    return s;
}

// Lime-washed plaster: white, hand-finished, streaked by the brush.
Surface makePlaster() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const int sweep = static_cast<int>(hnoise(x / 2, y / 9, 161) * 14);
            const int n = grain(x, y, 163, 7);
            s.at(x, y) = rgba(202 + sweep + n, 198 + sweep + n, 186 + sweep + n);
        }
    return s;
}

// An old white dust sheet: off-white cloth with soft fold shadows.
Surface makeSheet() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const double fold = std::sin(x * 0.22 + std::sin(y * 0.05) * 2.2) * 0.5 + 0.5;
            const int v = 196 + static_cast<int>(fold * 22) + grain(x, y, 139, 5);
            s.at(x, y) = rgba(v, v - 2, v - 8);
        }
    return s;
}

// Dark blue-black ink: stamps, writing, small printed details.
Surface makeInk() {
    Surface s(16, 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const int n = grain(x, y, 133, 6);
            s.at(x, y) = rgba(40 + n, 46 + n, 70 + n);
        }
    return s;
}

// An old mirror: dark silvered glass, a little lighter in the middle, foxed at the edges.
Surface makeMirror() {
    Surface s(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const double dx = (x - 31.5) / 32.0, dy = (y - 31.5) / 32.0;
            const double k = 0.5 + 0.5 * std::exp(-1.6 * (dx * dx + dy * dy));
            const double fox = hnoise(x / 3, y / 3, 151) > 0.82 && (std::fabs(dx) > 0.7 || std::fabs(dy) > 0.7) ? 0.6 : 1.0;
            const int n = grain(x, y, 153, 4);
            s.at(x, y) = rgba(static_cast<int>(96 * k * fox) + n, static_cast<int>(108 * k * fox) + n,
                              static_cast<int>(116 * k * fox) + n);
        }
    return s;
}

// Pure black: caps the dead end behind an open door so nothing (sky, fog) shows through.
Surface makeVoid() {
    Surface s(8, 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) s.at(x, y) = rgba(0, 0, 0);
    return s;
}

}  // namespace

void addCommonMaterials(TextureLibrary& lib) {
    lib.add("brick", makeBrick());
    lib.add("void", makeVoid());
    lib.add("wallpaper", makeWallpaper());
    lib.add("grass", makeGrass());
    lib.add("stone", makeStone());
    lib.add("door", makeDoor());
    lib.add("wood", makeWood());
    lib.add("tile", makeTile());
    lib.add("carpet", makeCarpet());
    lib.add("ceiling", makeCeiling());
    lib.add("enamel", makeEnamel());
    lib.add("metal", makeMetal());
    lib.add("paper", makePaper());
    lib.add("ink", makeInk());
    lib.add("cloth", makeCloth());
    lib.add("sheet", makeSheet());
    lib.add("wallpaper70s", makeWallpaper70s());
    lib.add("asphalt", makeAsphalt());
    lib.add("pine", makePine());
    lib.add("rock", makeRock());
    lib.add("plaster", makePlaster());
    lib.add("mirror", makeMirror());
    lib.add("poster", makePosterPlaceholder(), true);
    lib.add("painting", makePaintingPlaceholder(), true);
}

}  // namespace mythos
