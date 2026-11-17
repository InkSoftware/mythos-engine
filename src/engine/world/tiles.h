#pragma once
// The tile legend: what each map glyph means to the engine. A game describes its glyphs
// once (which block, which are doors, which textures they wear) and hands the legend to
// its levels; the engine never hard-codes a glyph beyond the '#' wall of the default legend.

#include <array>
#include <memory>
#include <string>

namespace mythos {

enum class DoorKind { None, Closed, Open };

struct TileInfo {
    bool solid = false;             // blocks movement, pathfinding and line of sight
    DoorKind door = DoorKind::None; // door cells get a doorway assembly instead of a wall block
    std::string wall;               // texture of a solid tile's faces ("" = the legend's default)
    bool empty = false;             // draws nothing at all (no floor, no walls): open air around a cab
    bool wallIsObject = false;      // stretched once over the wall (a picture) rather than tiled
    std::string floor;              // texture of a walkable tile's floor ("" = the legend's default)
    std::string sprite;             // billboard drawn on the cell ("" = none)
    float spriteSize = 0.34f;       // billboard width and height
    float spriteLift = 0.05f;       // height of its bottom edge above the floor
};

class TileSet {
public:
    TileSet() { tiles_['#'].solid = true; }

    TileInfo& operator[](char c) { return tiles_[static_cast<unsigned char>(c)]; }
    const TileInfo& operator[](char c) const { return tiles_[static_cast<unsigned char>(c)]; }

    bool solid(char c) const { return (*this)[c].solid; }
    bool isDoor(char c) const { return (*this)[c].door != DoorKind::None; }
    bool isOpenDoor(char c) const { return (*this)[c].door == DoorKind::Open; }
    bool isClosedDoor(char c) const { return (*this)[c].door == DoorKind::Closed; }

    const std::string& wallTexture(char c) const {
        const std::string& t = (*this)[c].wall;
        return t.empty() ? defaultWall : t;
    }
    const std::string& floorTexture(char c) const {
        const std::string& t = (*this)[c].floor;
        return t.empty() ? defaultFloor : t;
    }
    const std::string& spriteTexture(char c) const { return (*this)[c].sprite; }

    std::string defaultWall = "brick";
    std::string defaultFloor = "wood";

private:
    std::array<TileInfo, 256> tiles_{};
};

using TileSetPtr = std::shared_ptr<const TileSet>;

// A legend in which only '#' is solid. Levels use it until they are given another.
const TileSetPtr& defaultTileSet();

namespace json {
class Value;
}

// Reads a legend from JSON:
//   { "defaultWall": "brick", "defaultFloor": "wood",
//     "tiles": { "#": { "solid": true, "door": "closed"|"open", "wall": "...", "floor": "...",
//                       "wallIsObject": true, "sprite": "...", "spriteSize": 0.34, "spriteLift": 0.05 } } }
// Every field is optional; keys starting with '_' are comments. Unknown fields are errors.
bool parseTileSet(const json::Value& root, TileSet& out, std::string* error = nullptr);

}  // namespace mythos
