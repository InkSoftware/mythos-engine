#ifndef MYTHOS_ENGINE_GFX_MESH_H_
#define MYTHOS_ENGINE_GFX_MESH_H_

#include <string>
#include <vector>

#include "engine/world/level.h"

namespace mythos {
class ModelLibrary;
}

namespace mythos {
// Geometry for the 3D view. A level is turned into triangles here (pure CPU,
// testable); the platform layer uploads the vertices and draws them.
//
// Coordinates: world x = map column, world z = map row, world y = up. One map
// cell is one unit; walls are kWallHeight tall.
//
// Height of walls and of the floor-to-ceiling space, in meters.
constexpr float kWallHeight = 2.5f;

// Distance between the floors of a house stacked in one scene: a hair more than
// a wall, so the ceiling below and the floor above never share a plane.
constexpr float kFloorPitch = kWallHeight + 0.01f;

// One vertex: position, unit normal and texture coordinates. Models and the
// mesher both produce these.
struct Vertex {
  float px, py, pz;
  float nx, ny, nz;
  float u, v;
};

// Triangles that share one texture and one set of draw flags.
struct Batch {
  std::string texture;
  // Drawn at full brightness, unaffected by lighting.
  bool emissive = false;
  // Texels with alpha below 0.5 are discarded (sprites; a model material
  // "cutout_<texture>" sets it).
  bool alpha_test = false;
  // 1 is solid. Below 1 the batch is blended over what is behind it, in a pass
  // after the solid ones (glass, dust, mist cards, a ghost); it casts no
  // shadow. A model's material "blend<percent>_<texture>" sets it
  // ("blend35_dust" is the texture dust at 35 percent).
  float opacity = 1.0f;
  // Three vertices per triangle; there is no index buffer.
  std::vector<Vertex> verts;
};

// The static geometry of one scene.
struct SceneGeometry {
  // Walls, floors, ceilings, door frames and baked furniture.
  std::vector<Batch> world;
  // Sprites placed by tiles, as crossed billboards.
  std::vector<Batch> props;
  // Decals in placement order, drawn separately without casting shadows.
  std::vector<Batch> decals;
};

// Decal offsets locate the center relative to the face center in meters.
// U runs +x on north/floor/ceiling, -x on south, -z on west, +z on
// east; V runs +y on walls and +z on horizontal faces. Rotation is degrees
// counterclockwise in this U/V plane. Quads are not clipped to their cell.

// Builds the geometry of a level: floors, ceilings, walls, sprites and the
// furniture models, placed and turned as the level file says. Appearance of
// each glyph comes from the level's TileSet. An `outdoor` level gets no ceiling
// (the renderer draws the sky instead). Door cells become doorway assemblies
// (wall around a door-shaped hole, posts, lintel); the swinging panel itself is
// not included, see BuildDoorPanel. Furniture whose model is not in `models` is
// skipped.
SceneGeometry BuildSceneGeometry(const Level& level, const ModelLibrary& models,
                                 bool outdoor = false);

// As above, with every vertex raised by `y` meters: one floor of a house (floor
// index times kFloorPitch).
SceneGeometry BuildSceneGeometry(const Level& level, const ModelLibrary& models,
                                 bool outdoor, float y);

// Moves the batches of `from` onto `into`, joining the floors of a house into
// one scene.
void AppendSceneGeometry(SceneGeometry& into, SceneGeometry&& from);

// Size of the swinging door panel, in meters.
constexpr float kDoorPanelWidth = 0.70f;
constexpr float kDoorPanelHeight = 2.02f;

// The swinging door panel, hinged at the local origin and spanning local +x
// (kDoorPanelWidth by kDoorPanelHeight). The renderer places it at a door's
// hinge and turns it by the door's current swing angle.
std::vector<Batch> BuildDoorPanel();
}

#endif
