#ifndef MYTHOS_ENGINE_WORLD_TILES_H_
#define MYTHOS_ENGINE_WORLD_TILES_H_
// The tile legend: what each map glyph means to the engine. A game describes
// its glyphs once (which block, which are doors, which textures they wear) and
// hands the legend to its levels; the engine never hard-codes a glyph beyond
// the '#' wall of the default legend.

#include <array>
#include <memory>
#include <string>

#include "engine/core/json.h"

namespace mythos {

enum class DoorKind { kNone, kClosed, kOpen };

// How thick a thin wall is, in metres (a cell is one metre): a lath-and-plaster
// partition, not a metre of masonry.
constexpr float kThinWallThickness = 0.20f;

struct TileInfo {
  bool solid = false;  // blocks movement, pathfinding and line of sight
  DoorKind door = DoorKind::kNone;  // door cells get a doorway assembly instead
                                    // of a wall block
  std::string
      wall;  // texture of a solid tile's faces ("" = the legend's default)
  bool thin = false;    // a solid tile drawn and walled as a thin slab
                        // (kThinWallThickness) through the middle of its cell,
                        // joined to every solid or door neighbour, instead of
                        // a full-cell block: a real interior wall
  bool window = false;  // a thin wall with a window opening (and glass) in it;
                        // implies thin
  bool open_ceiling = false;  // a walkable tile with no ceiling: the well of a
                              // stair going up, seen through to the floor above
  bool open_floor = false;    // and no floor: the hole of a stair going down
  bool empty = false;  // draws nothing at all (no floor, no walls): open air
                       // around a cab
  bool wall_is_object =
      false;  // stretched once over the wall (a picture) rather than tiled
  std::string
      floor;  // texture of a walkable tile's floor ("" = the legend's default)
  std::string sprite;         // billboard drawn on the cell ("" = none)
  float sprite_size = 0.34f;  // billboard width and height
  float sprite_lift = 0.05f;  // height of its bottom edge above the floor
};

class TileSet {
 public:
  TileSet() { tiles_['#'].solid = true; }

  TileInfo& operator[](char c) { return tiles_[static_cast<unsigned char>(c)]; }
  const TileInfo& operator[](char c) const {
    return tiles_[static_cast<unsigned char>(c)];
  }

  bool Solid(char c) const { return (*this)[c].solid; }
  bool IsDoor(char c) const { return (*this)[c].door != DoorKind::kNone; }
  bool IsOpenDoor(char c) const { return (*this)[c].door == DoorKind::kOpen; }
  bool IsClosedDoor(char c) const {
    return (*this)[c].door == DoorKind::kClosed;
  }

  const std::string& WallTexture(char c) const {
    const std::string& t = (*this)[c].wall;
    return t.empty() ? default_wall : t;
  }
  const std::string& FloorTexture(char c) const {
    const std::string& t = (*this)[c].floor;
    return t.empty() ? default_floor : t;
  }
  const std::string& SpriteTexture(char c) const { return (*this)[c].sprite; }
  bool Thin(char c) const { return (*this)[c].thin || (*this)[c].window; }
  std::array<TileInfo, 256>& tiles() { return tiles_; }

  std::string default_wall = "brick";
  std::string default_floor = "wood";

 private:
  std::array<TileInfo, 256> tiles_{};
};

using TileSetPtr = std::shared_ptr<const TileSet>;

// A legend in which only '#' is solid. Levels use it until they are given
// another.
const TileSetPtr& DefaultTileSet();

// Reads a legend from JSON:
//   { "defaultWall": "brick", "defaultFloor": "wood",
//     "tiles": { "#": { "solid": true, "door": "closed"|"open", "wall": "...",
//     "floor": "...",
//                       "wallIsObject": true, "thin": true, "window": true,
//                       "sprite": "...", "spriteSize": 0.34, "spriteLift":
//                       0.05 } } }
// Every field is optional; keys starting with '_' are comments. Unknown fields
// are errors.
bool ParseTileSet(const json::Value& root, TileSet& out,
                  std::string* error = nullptr);

}  // namespace mythos

#endif  // MYTHOS_ENGINE_WORLD_TILES_H_
