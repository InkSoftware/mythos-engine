#include "engine/gfx/materials.h"

#include <cmath>
#include <cstdlib>
#include <cstdint>

#include "engine/gfx/font.h"
#include "engine/core/noise.h"

namespace mythos {
namespace {

// Every procedural texture tiles, so they are small squares unless the art needs more room.
constexpr int kTileSize = 64;

// Builds a texture by asking `colorAt(x, y)` for each pixel, row by row.
template <typename ColorAt>
Surface paint(int width, int height, ColorAt colorAt) {
    Surface surface(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) surface.at(x, y) = colorAt(x, y);
    return surface;
}

template <typename ColorAt>
Surface paintTile(ColorAt colorAt) {
    return paint(kTileSize, kTileSize, colorAt);
}

Surface makeBrick() {
    constexpr int kBrickRowHeight = 8;     // mortar line every 8 pixels down
    constexpr int kBrickLength = 16;       // bricks are twice as long as a row is tall
    constexpr int kBricksPerRowStride = 7; // gives each brick of each row its own id for tint noise
    return paintTile([](int x, int y) {
        const int row = y / kBrickRowHeight;
        const int rowOffset = (row & 1) * kBrickRowHeight;  // odd rows are shifted half a brick
        const int xInBrick = (x + rowOffset) % kBrickLength;
        const int brick = ((x + rowOffset) / kBrickLength) + row * kBricksPerRowStride;
        if (y % kBrickRowHeight == 0 || xInBrick == 0) {  // mortar
            const int v = 92 + grain(x, y, 11, 14);
            return rgba(v, v - 4, v - 10);
        }
        const double tint = 0.8 + 0.4 * hnoise(brick, 1, 7);
        const int g = grain(x, y, 3, 14);
        return rgba(static_cast<int>((118 + 40 * hnoise(brick, 2, 9)) * tint) + g,
                    static_cast<int>((52 + 14 * hnoise(brick, 3, 9)) * tint) + g / 2,
                    static_cast<int>((38 + 10 * hnoise(brick, 4, 9)) * tint) + g / 3);
    });
}

Surface makeWallpaper() {
    return paintTile([](int x, int y) {
        int r = 38, g = 62, b = 52;
        const double motif = std::sin(x * 0.39) * std::sin(y * 0.39);
        if (motif > 0.55) { r = 70; g = 96; b = 78; }
        if (x % 16 < 2) { r = r * 3 / 4; g = g * 3 / 4; b = b * 3 / 4; }  // stripe seam
        if (hnoise(x / 8, y / 8, 3) > 0.85) { r = r * 4 / 5; g = g * 4 / 5; b = b * 4 / 5; }  // water stain
        const int n = grain(x, y, 5, 6);
        return rgba(r + n, g + n, b + n);
    });
}

Surface makeStone() {
    return paintTile([](int x, int y) {
        int v = 105 + grain(x, y, 21, 22);
        if (hnoise(x / 2, y / 2, 23) > 0.97) v -= 45;  // crack
        if ((y / 16 + (x / 32) % 2) % 2 == 0 && y % 16 == 0) v -= 25;  // block joints
        return rgba(v, v + 2, v + 9);
    });
}

Surface makeWood() {
    constexpr int kPlankHeight = 16;
    return paintTile([](int x, int y) {
        const int plank = y / kPlankHeight;
        const double tint = 0.85 + 0.25 * hnoise(0, plank, 5);
        double v = 0.9 + 0.12 * std::sin(x * 0.31 + plank * 3.0 + std::sin(y * 0.4) * 1.5);
        if (y % kPlankHeight == 0) v *= 0.45;  // plank seam
        if ((x + plank * 17) % 21 == 0) v *= 0.8;  // a knot-like dark fleck, staggered per plank
        const int n = grain(x, y, 31, 6);
        return rgba(static_cast<int>(124 * tint * v) + n, static_cast<int>(82 * tint * v) + n,
                    static_cast<int>(46 * tint * v) + n);
    });
}

Surface makeTile() {
    constexpr int kTileCell = 16;  // one ceramic tile; the grout runs along its top and left edges
    return paintTile([](int x, int y) {
        if (x % kTileCell == 0 || y % kTileCell == 0) return rgba(70, 70, 72);  // grout
        const bool light = ((x / kTileCell) + (y / kTileCell)) % 2 == 0;  // chequerboard
        int v = light ? 218 : 42;
        v += grain(x, y, 41, 8);
        if (hnoise(x / 3, y / 3, 43) > 0.96) v -= 30;  // dirt
        return rgba(v, v - 5, light ? v - 20 : v + 4);
    });
}

Surface makeCarpet() {
    return paintTile([](int x, int y) {
        // Crossing diagonal stripes make a diamond lattice.
        const int diagonalDown = (x + y) % 16;
        const int diagonalUp = (x - y + kTileSize) % 16;
        const bool motif = diagonalDown < 2 || diagonalUp < 2;
        const int r = motif ? 140 : 108, g = motif ? 40 : 22, b = motif ? 40 : 30;
        const int n = grain(x, y, 51, 10);
        return rgba(r + n, g + n / 2, b + n / 2);
    });
}

// Night grass fallback for the yard. Normally replaced by assets/grass.bmp.
Surface makeGrass() {
    return paintTile([](int x, int y) {
        const double tuft = hnoise(x, y, 121) * 0.5 + hnoise(x / 3, y / 3, 123) * 0.5;
        const int v = static_cast<int>(40 + 46 * tuft);
        const int n = grain(x, y, 125, 8);
        return rgba(v / 2 + n / 2, v + n, v / 3 + n / 3);
    });
}

// Pine boughs at night: near-black green, ragged.
Surface makePine() {
    return paintTile([](int x, int y) {
        const double needles = hnoise(x / 2, y, 181) * 0.6 + hnoise(x, y / 3, 183) * 0.4;
        const int v = static_cast<int>(14 + 30 * needles);
        const int n = grain(x, y, 185, 4);
        return rgba(v / 2 + n / 2, v + n, v / 2 + n / 2);
    });
}

// Mountain rock at night: uneven grey with deep cracks, nothing laid in courses.
Surface makeRock() {
    return paintTile([](int x, int y) {
        const double mass = hnoise(x / 8, y / 8, 191) * 0.5 + hnoise(x / 3, y / 5, 193) * 0.35 +
                            hnoise(x, y, 195) * 0.15;
        int v = static_cast<int>(44 + 60 * mass);
        if (hnoise(x / 2, y / 9, 197) > 0.82) v -= 12;  // a crack
        const int n = grain(x, y, 199, 4);
        return rgba(v + n, v + n, v + 3 + n);
    });
}

Surface makeDoor() {
    Surface door = paintTile([](int x, int y) {
        const int plank = x / 16;
        int v = 96 + grain(plank, 0, 71, 8);
        // Two raised panels per 32-pixel half, one above the other.
        const bool inPanelColumn = x % 32 >= 6 && x % 32 < 26;
        const bool panel = (inPanelColumn && y >= 8 && y < 28) || (inPanelColumn && y >= 36 && y < 56);
        if (x % 16 == 0) v -= 25;  // plank seam
        if (panel) v += 14;
        return rgba(v, v * 62 / 96, v * 34 / 96);
    });
    // Gold handle: a small disc centred at (53, 32).
    constexpr int kHandleX = 53, kHandleY = 32;
    for (int y = 29; y < 35; ++y)
        for (int x = 50; x < 56; ++x)
            if ((x - kHandleX) * (x - kHandleX) + (y - kHandleY) * (y - kHandleY) <= 6) door.at(x, y) = rgba(220, 180, 60);
    return door;
}

Surface makeCeiling() {
    return paintTile([](int x, int y) {
        const int v = 48 + grain(x, y, 81, 8);
        if (hnoise(x / 6, y / 6, 77) > 0.8) return rgba(v + 22, v + 12, v - 4);  // water stain
        return rgba(v, v - 4, v - 6);
    });
}

// PLACEHOLDER poster: magenta frame with a label. Replace with assets/poster.bmp.
Surface makePosterPlaceholder() {
    Surface poster(kTileSize, kTileSize, rgba(200, 40, 140));
    for (int y = 0; y < kTileSize; ++y)
        for (int x = 0; x < kTileSize; ++x)
            if (x < 3 || y < 3 || x > 60 || y > 60) poster.at(x, y) = rgba(10, 10, 10);  // black border
    for (int y = 4; y < 60; ++y)
        for (int x = 4; x < 60; ++x)
            if (((x / 8) + (y / 8)) % 2 == 0) poster.at(x, y) = rgba(230, 200, 60);  // yellow chequer
    drawText(poster, 10, 22, "PLACE", rgba(20, 20, 20), 1);
    drawText(poster, 10, 32, "HOLDER", rgba(20, 20, 20), 1);
    return poster;
}

// PLACEHOLDER painting. Replace with assets/painting.bmp.
Surface makePaintingPlaceholder() {
    Surface painting(kTileSize, kTileSize, rgba(150, 120, 40));  // the frame
    for (int y = 4; y < 60; ++y)
        for (int x = 4; x < 60; ++x)
            painting.at(x, y) = (y < 36) ? rgba(90, 120, 150) : rgba(70, 100, 60);  // sky over field
    drawText(painting, 12, 14, "NO ART", rgba(240, 240, 220), 1);
    drawText(painting, 16, 44, "YET", rgba(240, 240, 220), 1);
    return painting;
}

// White enamel: fridges, toilets, sinks, washing machines.
Surface makeEnamel() {
    return paintTile([](int x, int y) {
        const int n = grain(x, y, 111, 4);
        return rgba(218 + n, 220 + n, 218 + n);
    });
}

// Brushed metal: cookers, handles, tv frames.
Surface makeMetal() {
    return paintTile([](int x, int y) {
        const int streak = static_cast<int>(hnoise(x, 0, 113) * 22);  // constant down each column
        const int n = grain(x, y, 115, 5);
        return rgba(120 + streak + n, 124 + streak + n, 130 + streak + n);
    });
}

// Cream writing paper with a faint laid texture: envelopes and letters.
Surface makePaper() {
    return paintTile([](int x, int y) {
        const int n = grain(x, y, 131, 5) + (y % 4 == 0 ? -4 : 0);
        return rgba(226 + n, 214 + n, 186 + n);
    });
}

// Dark wool: a man's good suit.
Surface makeCloth() {
    return paintTile([](int x, int y) {
        const int weave = ((x + y) % 4 < 2) ? 3 : -3;  // a faint twill
        const int n = grain(x, y, 137, 4) + weave;
        return rgba(30 + n, 32 + n, 40 + n);
    });
}

// Skin, for a figure's face and hands: a warm, slightly mottled tan with a little grain.
Surface makeSkin() {
    constexpr int kSkinSize = 128;
    return paint(kSkinSize, kSkinSize, [](int x, int y) {
        const double mottle = hnoise(x / 12, y / 12, 81) * 0.5 + hnoise(x / 4, y / 4, 83) * 0.25;
        const int base = static_cast<int>(150 + 34 * mottle);
        const int n = grain(x, y, 85, 5);
        return rgba(base + n, base * 3 / 4 + n, base * 3 / 5 + n);
    });
}

// A flame: yellow-white at the heart, orange at the edge (drawn glowing: "glow_flame").
Surface makeFlame() {
    return paint(16, 16, [](int x, int y) {
        const int distanceFromCentre = std::abs(x - 8) + std::abs(y - 8);  // a diamond
        return distanceFromCentre < 5 ? rgba(255, 210, 110) : rgba(255, 150, 50);
    });
}

// Wet asphalt at night: near-black, a worn centre line picking up the headlights.
Surface makeAsphalt() {
    return paintTile([](int x, int y) {
        const int n = grain(x, y, 167, 7) + static_cast<int>(hnoise(x / 4, y / 4, 169) * 10);
        int v = 34 + n;
        const bool inCentreLine = x >= 30 && x < 34;
        if (inCentreLine && y % 32 < 18 && hnoise(x, y, 171) > 0.3) v = 120 + n;  // the dashes, worn
        return rgba(v, v, v + 3);
    });
}

// 1970s wallpaper: big orange and brown diamonds on cream.
Surface makeWallpaper70s() {
    return paintTile([](int x, int y) {
        // Manhattan distance from the centre of each 32x32 repeat makes the diamond.
        const int d = std::abs((x % 32) - 16) + std::abs((y % 32) - 16);
        int r = 208, g = 188, b = 150;                     // cream ground
        if (d < 13) { r = 176; g = 108; b = 48; }          // orange diamond
        if (d < 7) { r = 120; g = 74; b = 40; }            // brown core
        if (d == 13 || d == 7) { r = 92; g = 58; b = 34; } // outlines
        const int n = grain(x, y, 157, 5);
        return rgba(r + n, g + n, b + n);
    });
}

// Lime-washed plaster: white, hand-finished, streaked by the brush.
Surface makePlaster() {
    return paintTile([](int x, int y) {
        const int sweep = static_cast<int>(hnoise(x / 2, y / 9, 161) * 14);  // tall thin brush strokes
        const int n = grain(x, y, 163, 7);
        return rgba(202 + sweep + n, 198 + sweep + n, 186 + sweep + n);
    });
}

// An old white dust sheet: off-white cloth with soft fold shadows.
Surface makeSheet() {
    return paintTile([](int x, int y) {
        const double fold = std::sin(x * 0.22 + std::sin(y * 0.05) * 2.2) * 0.5 + 0.5;
        const int v = 196 + static_cast<int>(fold * 22) + grain(x, y, 139, 5);
        return rgba(v, v - 2, v - 8);
    });
}

// Dark blue-black ink: stamps, writing, small printed details.
Surface makeInk() {
    return paint(16, 16, [](int x, int y) {
        const int n = grain(x, y, 133, 6);
        return rgba(40 + n, 46 + n, 70 + n);
    });
}

// An old mirror: dark silvered glass, a little lighter in the middle, foxed at the edges.
Surface makeMirror() {
    return paintTile([](int x, int y) {
        // Position from the centre, -1..1 across the texture.
        const double dx = (x - 31.5) / 32.0, dy = (y - 31.5) / 32.0;
        const double brightness = 0.5 + 0.5 * std::exp(-1.6 * (dx * dx + dy * dy));
        const bool foxed = hnoise(x / 3, y / 3, 151) > 0.82 && (std::fabs(dx) > 0.7 || std::fabs(dy) > 0.7);
        const double fox = foxed ? 0.6 : 1.0;
        const int n = grain(x, y, 153, 4);
        return rgba(static_cast<int>(96 * brightness * fox) + n, static_cast<int>(108 * brightness * fox) + n,
                    static_cast<int>(116 * brightness * fox) + n);
    });
}

// Pure black: caps the dead end behind an open door so nothing (sky, fog) shows through.
Surface makeVoid() { return Surface(8, 8, rgba(0, 0, 0)); }

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
    lib.add("skin", makeSkin());
    lib.add("flame", makeFlame());
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
