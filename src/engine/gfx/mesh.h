#ifndef MYTHOS_ENGINE_GFX_MESH_H_
#define MYTHOS_ENGINE_GFX_MESH_H_
// Geometry for the 3D view. A level is turned into triangles here (pure CPU,
// testable); the platform layer uploads the vertices to the GPU and draws them.
//
// Coordinates: world x = map column, world z = map row, world y = up.
// One map cell is one unit; walls are kWallHeight tall.

#include <string>
#include <vector>

#include "engine/world/level.h"

namespace mythos {
class ModelLibrary;
}

namespace mythos {

constexpr float kWallHeight = 2.6f;

struct Vertex {
  float px, py, pz;
  float nx, ny, nz;
  float u, v;
};

// Triangles that share one texture and one set of draw flags.
struct Batch {
  std::string texture;
  bool emissive = false;      // drawn at full brightness (eyes)
  bool alpha_test = false;    // texels with alpha < 0.5 are discarded (sprites)
  std::vector<Vertex> verts;  // 3 vertices per triangle, no index buffer
};

struct SceneGeometry {
  std::vector<Batch> world;  // walls, floors, ceilings, doors
  std::vector<Batch> props;  // items and the exit gate, as crossed billboards
};

// Builds the world geometry of a level: floors, ceilings, walls, sprites and
// all furniture models, placed and turned as the level file says. Furniture is
// baked into the world. What each glyph looks like comes from the level's
// TileSet. An outdoor level gets no ceiling: the renderer draws the night sky
// there instead. Door cells (closed or open, per the TileSet) become doorway
// assemblies: wall fillers around a door-shaped hole, wooden posts and a
// lintel. The swinging panel itself is NOT part of the static world; the
// renderer draws it per frame (see buildDoorPanel). Room openings get wooden
// door frames too.
SceneGeometry BuildSceneGeometry(const Level& level, const ModelLibrary& models,
                                 bool outdoor = false);

// The swinging door panel, hinged at the local origin, spanning local +x (width
// kDoorPanelWidth, height kDoorPanelHeight). The renderer places it at a door's
// hinge and turns it by the door's current swing angle.
constexpr float kDoorPanelWidth = 0.70f;
constexpr float kDoorPanelHeight = 2.02f;
std::vector<Batch> BuildDoorPanel();

}  // namespace mythos

#endif  // MYTHOS_ENGINE_GFX_MESH_H_
