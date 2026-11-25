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
#include <vector>

#include "engine/gfx/probes.h"
#include "engine/world/level.h"

namespace mythos {
// The six directions a lit surface can face, in layer order: +x (a wall on a
// cell's west side), -x (east side), +y (the floor), -y (the ceiling), +z
// (north side), -z (south side).
inline constexpr int kLightmapFaces = 6;

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
  // kLightmapFaces layers of Width() x Height() RGBA floats (alpha 1 where a
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
}

#endif
