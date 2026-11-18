#include "engine/gfx/materials.h"

#include <cmath>
#include <cstdlib>

#include "engine/core/noise.h"
#include "engine/gfx/font.h"

namespace mythos {
namespace {

// Every procedural texture tiles, so they are small squares unless the art
// needs more room.
constexpr int kTileSize = 64;

// Builds a texture by asking `colorAt(x, y)` for each pixel, row by row.
template <typename ColorAt>
Surface Paint(int width, int height, ColorAt color_at) {
  Surface surface(width, height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) surface.At(x, y) = color_at(x, y);
  }
  return surface;
}

template <typename ColorAt>
Surface PaintTile(ColorAt color_at) {
  return Paint(kTileSize, kTileSize, color_at);
}

Surface MakeBrick() {
  constexpr int kBrickRowHeight = 8;  // mortar line every 8 pixels down
  constexpr int kBrickLength = 16;  // bricks are twice as long as a row is tall
  constexpr int kBricksPerRowStride =
      7;  // gives each brick of each row its own id for tint noise
  return PaintTile([](int x, int y) {
    const int row = y / kBrickRowHeight;
    const int row_offset =
        (row & 1) * kBrickRowHeight;  // odd rows are shifted half a brick
    const int x_in_brick = (x + row_offset) % kBrickLength;
    const int brick =
        ((x + row_offset) / kBrickLength) + row * kBricksPerRowStride;
    if (y % kBrickRowHeight == 0 || x_in_brick == 0) {  // mortar
      const int v = 92 + Grain(x, y, 11, 14);
      return Rgba(v, v - 4, v - 10);
    }
    const double tint = 0.8 + 0.4 * Hnoise(brick, 1, 7);
    const int g = Grain(x, y, 3, 14);
    return Rgba(
        static_cast<int>((118 + 40 * Hnoise(brick, 2, 9)) * tint) + g,
        static_cast<int>((52 + 14 * Hnoise(brick, 3, 9)) * tint) + g / 2,
        static_cast<int>((38 + 10 * Hnoise(brick, 4, 9)) * tint) + g / 3);
  });
}

Surface MakeWallpaper() {
  return PaintTile([](int x, int y) {
    int r = 38, g = 62, b = 52;
    const double motif = std::sin(x * 0.39) * std::sin(y * 0.39);
    if (motif > 0.55) {
      r = 70;
      g = 96;
      b = 78;
    }
    if (x % 16 < 2) {
      r = r * 3 / 4;
      g = g * 3 / 4;
      b = b * 3 / 4;
    }  // stripe seam
    if (Hnoise(x / 8, y / 8, 3) > 0.85) {
      r = r * 4 / 5;
      g = g * 4 / 5;
      b = b * 4 / 5;
    }  // water stain
    const int n = Grain(x, y, 5, 6);
    return Rgba(r + n, g + n, b + n);
  });
}

Surface MakeStone() {
  return PaintTile([](int x, int y) {
    int v = 105 + Grain(x, y, 21, 22);
    if (Hnoise(x / 2, y / 2, 23) > 0.97) v -= 45;  // crack
    if ((y / 16 + (x / 32) % 2) % 2 == 0 && y % 16 == 0) {
      v -= 25;  // block joints
    }
    return Rgba(v, v + 2, v + 9);
  });
}

Surface MakeWood() {
  constexpr int kPlankHeight = 16;
  return PaintTile([](int x, int y) {
    const int plank = y / kPlankHeight;
    const double tint = 0.85 + 0.25 * Hnoise(0, plank, 5);
    double v =
        0.9 + 0.12 * std::sin(x * 0.31 + plank * 3.0 + std::sin(y * 0.4) * 1.5);
    if (y % kPlankHeight == 0) v *= 0.45;  // plank seam
    if ((x + plank * 17) % 21 == 0) {
      v *= 0.8;  // a knot-like dark fleck, staggered per plank
    }
    const int n = Grain(x, y, 31, 6);
    return Rgba(static_cast<int>(124 * tint * v) + n,
                static_cast<int>(82 * tint * v) + n,
                static_cast<int>(46 * tint * v) + n);
  });
}

Surface MakeTile() {
  constexpr int kTileCell =
      16;  // one ceramic tile; the grout runs along its top and left edges
  return PaintTile([](int x, int y) {
    if (x % kTileCell == 0 || y % kTileCell == 0) {
      return Rgba(70, 70, 72);  // grout
    }
    const bool light =
        ((x / kTileCell) + (y / kTileCell)) % 2 == 0;  // chequerboard
    int v = light ? 218 : 42;
    v += Grain(x, y, 41, 8);
    if (Hnoise(x / 3, y / 3, 43) > 0.96) v -= 30;  // dirt
    return Rgba(v, v - 5, light ? v - 20 : v + 4);
  });
}

Surface MakeCarpet() {
  return PaintTile([](int x, int y) {
    // Crossing diagonal stripes make a diamond lattice.
    const int diagonal_down = (x + y) % 16;
    const int diagonal_up = (x - y + kTileSize) % 16;
    const bool motif = diagonal_down < 2 || diagonal_up < 2;
    const int r = motif ? 140 : 108, g = motif ? 40 : 22, b = motif ? 40 : 30;
    const int n = Grain(x, y, 51, 10);
    return Rgba(r + n, g + n / 2, b + n / 2);
  });
}

// Night grass fallback for the yard. Normally replaced by assets/grass.bmp.
Surface MakeGrass() {
  return PaintTile([](int x, int y) {
    const double tuft =
        Hnoise(x, y, 121) * 0.5 + Hnoise(x / 3, y / 3, 123) * 0.5;
    const int v = static_cast<int>(40 + 46 * tuft);
    const int n = Grain(x, y, 125, 8);
    return Rgba(v / 2 + n / 2, v + n, v / 3 + n / 3);
  });
}

// Pine boughs at night: near-black green, ragged.
Surface MakePine() {
  return PaintTile([](int x, int y) {
    const double needles =
        Hnoise(x / 2, y, 181) * 0.6 + Hnoise(x, y / 3, 183) * 0.4;
    const int v = static_cast<int>(14 + 30 * needles);
    const int n = Grain(x, y, 185, 4);
    return Rgba(v / 2 + n / 2, v + n, v / 2 + n / 2);
  });
}

// Mountain rock at night: uneven grey with deep cracks, nothing laid in
// courses.
Surface MakeRock() {
  return PaintTile([](int x, int y) {
    const double mass = Hnoise(x / 8, y / 8, 191) * 0.5 +
                        Hnoise(x / 3, y / 5, 193) * 0.35 +
                        Hnoise(x, y, 195) * 0.15;
    int v = static_cast<int>(44 + 60 * mass);
    if (Hnoise(x / 2, y / 9, 197) > 0.82) v -= 12;  // a crack
    const int n = Grain(x, y, 199, 4);
    return Rgba(v + n, v + n, v + 3 + n);
  });
}

Surface MakeDoor() {
  Surface door = PaintTile([](int x, int y) {
    const int plank = x / 16;
    int v = 96 + Grain(plank, 0, 71, 8);
    // Two raised panels per 32-pixel half, one above the other.
    const bool in_panel_column = x % 32 >= 6 && x % 32 < 26;
    const bool panel = (in_panel_column && y >= 8 && y < 28) ||
                       (in_panel_column && y >= 36 && y < 56);
    if (x % 16 == 0) v -= 25;  // plank seam
    if (panel) v += 14;
    return Rgba(v, v * 62 / 96, v * 34 / 96);
  });
  // Gold handle: a small disc centred at (53, 32).
  constexpr int kHandleX = 53, kHandleY = 32;
  for (int y = 29; y < 35; ++y) {
    for (int x = 50; x < 56; ++x) {
      if ((x - kHandleX) * (x - kHandleX) + (y - kHandleY) * (y - kHandleY) <=
          6) {
        door.At(x, y) = Rgba(220, 180, 60);
      }
    }
  }
  return door;
}

Surface MakeCeiling() {
  return PaintTile([](int x, int y) {
    const int v = 48 + Grain(x, y, 81, 8);
    if (Hnoise(x / 6, y / 6, 77) > 0.8) {
      return Rgba(v + 22, v + 12, v - 4);  // water stain
    }
    return Rgba(v, v - 4, v - 6);
  });
}

// PLACEHOLDER poster: magenta frame with a label. Replace with
// assets/poster.bmp.
Surface MakePosterPlaceholder() {
  Surface poster(kTileSize, kTileSize, Rgba(200, 40, 140));
  for (int y = 0; y < kTileSize; ++y) {
    for (int x = 0; x < kTileSize; ++x) {
      if (x < 3 || y < 3 || x > 60 || y > 60) {
        poster.At(x, y) = Rgba(10, 10, 10);  // black border
      }
    }
  }
  for (int y = 4; y < 60; ++y) {
    for (int x = 4; x < 60; ++x) {
      if (((x / 8) + (y / 8)) % 2 == 0) {
        poster.At(x, y) = Rgba(230, 200, 60);  // yellow chequer
      }
    }
  }
  DrawText(poster, 10, 22, "PLACE", Rgba(20, 20, 20), 1);
  DrawText(poster, 10, 32, "HOLDER", Rgba(20, 20, 20), 1);
  return poster;
}

// PLACEHOLDER painting. Replace with assets/painting.bmp.
Surface MakePaintingPlaceholder() {
  Surface painting(kTileSize, kTileSize, Rgba(150, 120, 40));  // the frame
  for (int y = 4; y < 60; ++y) {
    for (int x = 4; x < 60; ++x) {
      painting.At(x, y) =
          (y < 36) ? Rgba(90, 120, 150) : Rgba(70, 100, 60);  // sky over field
    }
  }
  DrawText(painting, 12, 14, "NO ART", Rgba(240, 240, 220), 1);
  DrawText(painting, 16, 44, "YET", Rgba(240, 240, 220), 1);
  return painting;
}

// White enamel: fridges, toilets, sinks, washing machines.
Surface MakeEnamel() {
  return PaintTile([](int x, int y) {
    const int n = Grain(x, y, 111, 4);
    return Rgba(218 + n, 220 + n, 218 + n);
  });
}

// Brushed metal: cookers, handles, tv frames.
Surface MakeMetal() {
  return PaintTile([](int x, int y) {
    const int streak =
        static_cast<int>(Hnoise(x, 0, 113) * 22);  // constant down each column
    const int n = Grain(x, y, 115, 5);
    return Rgba(120 + streak + n, 124 + streak + n, 130 + streak + n);
  });
}

// Cream writing paper with a faint laid texture: envelopes and letters.
Surface MakePaper() {
  return PaintTile([](int x, int y) {
    const int n = Grain(x, y, 131, 5) + (y % 4 == 0 ? -4 : 0);
    return Rgba(226 + n, 214 + n, 186 + n);
  });
}

// Dark wool: a man's good suit.
Surface MakeCloth() {
  return PaintTile([](int x, int y) {
    const int weave = ((x + y) % 4 < 2) ? 3 : -3;  // a faint twill
    const int n = Grain(x, y, 137, 4) + weave;
    return Rgba(30 + n, 32 + n, 40 + n);
  });
}

// Skin, for a figure's face and hands: a warm, slightly mottled tan with a
// little grain.
Surface MakeSkin() {
  constexpr int kSkinSize = 128;
  return Paint(kSkinSize, kSkinSize, [](int x, int y) {
    const double mottle =
        Hnoise(x / 12, y / 12, 81) * 0.5 + Hnoise(x / 4, y / 4, 83) * 0.25;
    const int base = static_cast<int>(150 + 34 * mottle);
    const int n = Grain(x, y, 85, 5);
    return Rgba(base + n, base * 3 / 4 + n, base * 3 / 5 + n);
  });
}

// A flame: yellow-white at the heart, orange at the edge (drawn glowing:
// "glow_flame").
Surface MakeFlame() {
  return Paint(16, 16, [](int x, int y) {
    const int distance_from_centre =
        std::abs(x - 8) + std::abs(y - 8);  // a diamond
    return distance_from_centre < 5 ? Rgba(255, 210, 110) : Rgba(255, 150, 50);
  });
}

// Wet asphalt at night: near-black, a worn centre line picking up the
// headlights.
Surface MakeAsphalt() {
  return PaintTile([](int x, int y) {
    const int n =
        Grain(x, y, 167, 7) + static_cast<int>(Hnoise(x / 4, y / 4, 169) * 10);
    int v = 34 + n;
    const bool in_centre_line = x >= 30 && x < 34;
    if (in_centre_line && y % 32 < 18 && Hnoise(x, y, 171) > 0.3) {
      v = 120 + n;  // the dashes, worn
    }
    return Rgba(v, v, v + 3);
  });
}

// 1970s wallpaper: big orange and brown diamonds on cream.
Surface MakeWallpaper70s() {
  return PaintTile([](int x, int y) {
    // Manhattan distance from the centre of each 32x32 repeat makes the
    // diamond.
    const int d = std::abs((x % 32) - 16) + std::abs((y % 32) - 16);
    int r = 208, g = 188, b = 150;  // cream ground
    if (d < 13) {
      r = 176;
      g = 108;
      b = 48;
    }  // orange diamond
    if (d < 7) {
      r = 120;
      g = 74;
      b = 40;
    }  // brown core
    if (d == 13 || d == 7) {
      r = 92;
      g = 58;
      b = 34;
    }  // outlines
    const int n = Grain(x, y, 157, 5);
    return Rgba(r + n, g + n, b + n);
  });
}

// Lime-washed plaster: white, hand-finished, streaked by the brush.
Surface MakePlaster() {
  return PaintTile([](int x, int y) {
    const int sweep = static_cast<int>(Hnoise(x / 2, y / 9, 161) *
                                       14);  // tall thin brush strokes
    const int n = Grain(x, y, 163, 7);
    return Rgba(202 + sweep + n, 198 + sweep + n, 186 + sweep + n);
  });
}

// An old white dust sheet: off-white cloth with soft fold shadows.
Surface MakeSheet() {
  return PaintTile([](int x, int y) {
    const double fold =
        std::sin(x * 0.22 + std::sin(y * 0.05) * 2.2) * 0.5 + 0.5;
    const int v = 196 + static_cast<int>(fold * 22) + Grain(x, y, 139, 5);
    return Rgba(v, v - 2, v - 8);
  });
}

// Dark blue-black ink: stamps, writing, small printed details.
Surface MakeInk() {
  return Paint(16, 16, [](int x, int y) {
    const int n = Grain(x, y, 133, 6);
    return Rgba(40 + n, 46 + n, 70 + n);
  });
}

// An old mirror: dark silvered glass, a little lighter in the middle, foxed at
// the edges.
Surface MakeMirror() {
  return PaintTile([](int x, int y) {
    // Position from the centre, -1..1 across the texture.
    const double dx = (x - 31.5) / 32.0, dy = (y - 31.5) / 32.0;
    const double brightness = 0.5 + 0.5 * std::exp(-1.6 * (dx * dx + dy * dy));
    const bool foxed = Hnoise(x / 3, y / 3, 151) > 0.82 &&
                       (std::fabs(dx) > 0.7 || std::fabs(dy) > 0.7);
    const double fox = foxed ? 0.6 : 1.0;
    const int n = Grain(x, y, 153, 4);
    return Rgba(static_cast<int>(96 * brightness * fox) + n,
                static_cast<int>(108 * brightness * fox) + n,
                static_cast<int>(116 * brightness * fox) + n);
  });
}

// Pure black: caps the dead end behind an open door so nothing (sky, fog) shows
// through.
Surface MakeVoid() { return Surface(8, 8, Rgba(0, 0, 0)); }

}  // namespace

void AddCommonMaterials(TextureLibrary& lib) {
  lib.Add("brick", MakeBrick());
  lib.Add("void", MakeVoid());
  lib.Add("wallpaper", MakeWallpaper());
  lib.Add("grass", MakeGrass());
  lib.Add("stone", MakeStone());
  lib.Add("door", MakeDoor());
  lib.Add("wood", MakeWood());
  lib.Add("tile", MakeTile());
  lib.Add("carpet", MakeCarpet());
  lib.Add("ceiling", MakeCeiling());
  lib.Add("enamel", MakeEnamel());
  lib.Add("metal", MakeMetal());
  lib.Add("paper", MakePaper());
  lib.Add("ink", MakeInk());
  lib.Add("cloth", MakeCloth());
  lib.Add("skin", MakeSkin());
  lib.Add("flame", MakeFlame());
  lib.Add("sheet", MakeSheet());
  lib.Add("wallpaper70s", MakeWallpaper70s());
  lib.Add("asphalt", MakeAsphalt());
  lib.Add("pine", MakePine());
  lib.Add("rock", MakeRock());
  lib.Add("plaster", MakePlaster());
  lib.Add("mirror", MakeMirror());
  lib.Add("poster", MakePosterPlaceholder(), true);
  lib.Add("painting", MakePaintingPlaceholder(), true);
}

}  // namespace mythos
