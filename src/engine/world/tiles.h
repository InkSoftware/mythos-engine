#ifndef MYTHOS_ENGINE_WORLD_TILES_H_
#define MYTHOS_ENGINE_WORLD_TILES_H_

#include <array>
#include <memory>
#include <string>

#include "engine/core/json.h"

// Tile legend: what each map glyph means (solid, door, textures, flags). Games
// load it from JSON with ParseTileSet; the engine never hardcodes a game's
// glyphs.
namespace mythos {
// Whether a glyph is a door and in which state. The room-door system swaps the
// closed and open glyphs as a door swings.
enum class DoorKind { kNone, kClosed, kOpen };

// Thickness in meters of a thin wall or window slab, centerd in its cell.
constexpr float kThinWallThickness = 0.20f;

// Properties of one glyph. Texture names are looked up in the texture registry;
// an empty name falls back to the TileSet default.
struct TileInfo {
  // Blocks movement and is meshed as a wall.
  bool solid = false;
  DoorKind door = DoorKind::kNone;

  // Wall texture, or empty for TileSet::default_wall.
  std::string wall;
  // A solid cell drawn and collided as a thin slab (see Level::ThinRects).
  bool thin = false;

  // A thin wall with a sill, jambs, frame and glass quad. Implies thin.
  bool window = false;

  // No ceiling over this cell (a stairwell).
  bool open_ceiling = false;

  // No floor under this cell (a stairwell).
  bool open_floor = false;
  // Draws nothing at all (no floor, no walls): open air.
  bool empty = false;

  // The wall texture is one object (a picture): stretched once over the wall
  // instead of tiled.
  bool wall_is_object = false;
  // Floor texture, or empty for TileSet::default_floor.
  std::string floor;
  // Billboard texture drawn in the cell, or empty for none.
  std::string sprite;
  // Sprite size in meters.
  float sprite_size = 0.34f;
  // Height of the sprite above the floor, in meters.
  float sprite_lift = 0.05f;
};

// Table of TileInfo indexed by glyph byte. Unlisted glyphs are non-solid with
// default textures; '#' is solid by default.
class TileSet {
 public:
  TileSet() { tiles_['#'].solid = true; }

  TileInfo& operator[](const char c) {
    return tiles_[static_cast<unsigned char>(c)];
  }

  const TileInfo& operator[](const char c) const {
    return tiles_[static_cast<unsigned char>(c)];
  }

  [[nodiscard]] bool Solid(const char c) const { return (*this)[c].solid; }

  [[nodiscard]] bool IsDoor(const char c) const {
    return (*this)[c].door != DoorKind::kNone;
  }

  [[nodiscard]] bool IsOpenDoor(const char c) const {
    return (*this)[c].door == DoorKind::kOpen;
  }

  [[nodiscard]] bool IsClosedDoor(const char c) const {
    return (*this)[c].door == DoorKind::kClosed;
  }

  [[nodiscard]] const std::string& WallTexture(const char c) const {
    const std::string& t = (*this)[c].wall;
    return t.empty() ? default_wall : t;
  }

  [[nodiscard]] const std::string& FloorTexture(const char c) const {
    const std::string& t = (*this)[c].floor;
    return t.empty() ? default_floor : t;
  }

  [[nodiscard]] const std::string& SpriteTexture(const char c) const {
    return (*this)[c].sprite;
  }

  // True for a thin wall or a window.
  [[nodiscard]] bool Thin(const char c) const {
    return (*this)[c].thin || (*this)[c].window;
  }

  // Mutable access to the whole table, for the parser.
  std::array<TileInfo, 256>& tiles() { return tiles_; }

  // Textures used by glyphs that name none.
  std::string default_wall = "brick";
  std::string default_floor = "wood";

 private:
  std::array<TileInfo, 256> tiles_{};
};

// Shared immutable tile set; a Level and the renderer may hold the same one.
using TileSetPtr = std::shared_ptr<const TileSet>;

// The built-in tile set (only '#' is solid), used when a level has none.
const TileSetPtr& DefaultTileSet();

// Parses a tile legend object (keys "defaultWall", "defaultFloor", "tiles")
// into out, replacing it; unlike the default set, '#' starts non-solid. Keys
// starting with '_' are comments. Returns false and sets *error (if given) on
// an unknown key or a wrongly typed field.
bool ParseTileSet(const json::Value& root, TileSet& out,
                  std::string* error = nullptr);
}

#endif
