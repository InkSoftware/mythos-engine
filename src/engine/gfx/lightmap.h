// Baked light on the surfaces themselves: lightmaps for the walls, floors and
// ceilings of a level, computed once on the CPU from static lamps (with the
// walls between them and each texel taken into account, so it holds sharp
// shadows a probe grid cannot). The mesher's vertices carry no second set of
// texture coordinates, so the maps are addressed by where a point is: every
// open cell has up to six tiles, one for each direction a surface in it can
// face (a wall or the floor or the ceiling), and a fragment finds its tile
// from its world position and normal. Models and thin walls are not on any
// tile (`covered` is false there); a game lights those with probes
// (gfx/probes.h). Headless; `SceneRenderer::UploadLightmap` uploads it.
#ifndef MYTHOS_ENGINE_GFX_LIGHTMAP_H_
#define MYTHOS_ENGINE_GFX_LIGHTMAP_H_

#include <array>
#include <string>
#include <vector>

#include "engine/gfx/probes.h"
#include "engine/world/level.h"

namespace mythos {
// The six directions a lit surface can face, in layer order: +x (a wall on a
// cell's west side), -x (east side), +y (the floor), -y (the ceiling), +z
// (north side), -z (south side).
inline constexpr int kLightmapFaces = 6;
// Faces inside a cell get tiles of their own, as layers after the six, each
// covering the whole cell along the plane: a thin wall's four faces (6 +x on
// the plane 0.6 into the cell, 7 -x on 0.4, 8 +z on 0.6, 9 -z on 0.4) and a
// doorway's inner jamb faces (10 +x on 0.15, 11 -x on 0.85, 12 +z on 0.15,
// 13 -z on 0.85).
inline constexpr int kLightmapLayers = kLightmapFaces + 8;

struct LightmapOptions {
  // Texels a metre. Halved (down to 2) until the maps fit about 8 million
  // texels, so a large level keeps a smaller map.
  int texels_per_m = 8;
  // Light every texel gets whatever the lamps do.
  std::array<float, 3> ambient{0, 0, 0};
};

struct Lightmap {
  int cells_x = 0, cells_z = 0;
  // Texels a metre, and rows of a wall tile (the wall height times that);
  // every tile slot is `texels` wide and `wall_rows` tall, a floor or ceiling
  // using the first `texels` rows.
  int texels = 8, wall_rows = 20;
  // kLightmapLayers layers of Width() x Height() RGBA floats (alpha 1 where a
  // texel was baked, 0 where the tile does not exist).
  std::vector<float> rgba;

  [[nodiscard]] bool Empty() const { return cells_x <= 0 || cells_z <= 0; }
  [[nodiscard]] int Width() const { return cells_x * texels; }
  [[nodiscard]] int Height() const { return cells_z * wall_rows; }
  [[nodiscard]] std::array<float, 4> Texel(int face, int x, int y) const;
  // The light on a surface at (x, y, z) with unit normal (nx, ny, nz): the
  // renderer's lookup (the tile by cell and facing, bilinear inside the tile).
  // `covered` tells whether the point is on a baked tile; if not the colour is
  // 0.
  [[nodiscard]] std::array<float, 3> Sample(double x, double y, double z,
                                            double nx, double ny, double nz,
                                            bool* covered = nullptr) const;
};

// Bakes `level`. Each texel gets every lamp's colour times the renderer's
// falloff `(1 - d / radius)^2` and angle term `0.35 + 0.65 * (l . n)` when the
// line from the lamp to a point just off the surface is clear.
Lightmap BakeLightmap(const Level& level, const std::vector<ProbeLamp>& lamps,
                      const LightmapOptions& options = {});

// The lightmaps of a house of stacked floors (`kFloorPitch` apart in y, the
// way `AppendSceneGeometry` joins them): one `Lightmap` a floor, each baked
// from that floor's level with its lamps' heights taken from the floor (y
// from 0), all of one size. `Sample` picks the floor by y and reads it at the
// height above that floor.
struct LightmapStack {
  std::vector<Lightmap> floors;
  double pitch = 0;  // kFloorPitch when 0

  [[nodiscard]] bool Empty() const {
    return floors.empty() || floors[0].Empty();
  }
  [[nodiscard]] std::array<float, 3> Sample(double x, double y, double z,
                                            double nx, double ny, double nz,
                                            bool* covered = nullptr) const;
};

// A lightmap as bytes for a file: the magic `MYLM1`, the cell counts, texels
// and wall rows as 32-bit little-endian integers, then the layers as 16-bit
// half floats (RGBA, as the GPU holds them). `ParseLightmap` reads it back
// (to half precision) and returns false, leaving `out`, on a bad or short file.
std::string SerializeLightmap(const Lightmap& map);
bool ParseLightmap(const std::string& bytes, Lightmap* out,
                   std::string* error = nullptr);
}

#endif
