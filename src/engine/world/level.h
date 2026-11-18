#pragma once
// Grid level model: a map of glyphs plus furniture, parsed from text level files.
// What each glyph means (solid, door, textures) comes from the level's TileSet.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "engine/world/tiles.h"

namespace mythos {

// A model placed in the level. Furniture fills one solid cell and stands on its centre.
// A prop (solid = false) is a small object that blocks nothing: it may sit off the cell
// centre (offX, offZ) and above the floor (lift), on a table or on a wall.
struct Placement {
    int x = 0;
    int y = 0;
    std::string model;
    double yawDeg = 0;
    double offX = 0, offZ = 0;  // from the cell centre, in world units
    double lift = 0;            // height of the model's base above the floor
    bool solid = true;
    std::string id;             // names a prop, so it can be removed again
};

class Level {
public:
    // Builds the grid from rows. All rows must have the same length. Clears furniture.
    bool parse(const std::vector<std::string>& rows, std::string* error = nullptr);
    // Parses a zone file: a [map] section of rows, a [furniture] section of "x y model yaw" lines (solid, one cell each) and
    // an optional [decor] section of "x y model yaw [offX offZ lift]" lines (non-solid: a vase on a table, a picture on a wall).
    // Lines starting with ';' are comments.
    bool parseText(const std::string& text, std::string* error = nullptr);

    // Parses the level file at path (see parseText).
    bool loadFile(const std::string& path, std::string* error = nullptr);

    // The glyph legend. Kept across parse() and copies; defaults to defaultTileSet().
    void setTiles(TileSetPtr tiles) { tiles_ = tiles ? std::move(tiles) : defaultTileSet(); }
    const TileSet& tiles() const { return *tiles_; }
    const TileSetPtr& tilesPtr() const { return tiles_; }

    int width() const { return w_; }
    int height() const { return h_; }
    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }
    // Out-of-bounds reads return a wall.
    char at(int x, int y) const;
    void set(int x, int y, char c);
    // Passable for movement: not a solid glyph, and not covered by furniture.
    bool passable(int x, int y) const;

    // First cell (row-major) holding glyph c.
    bool find(char c, int& x, int& y) const;
    std::vector<std::pair<int, int>> findAll(char c) const;

    const std::vector<Placement>& furniture() const { return furniture_; }
    // Solid furniture covering (x, y), or nullptr. Props are never returned.
    const Placement* furnitureAt(int x, int y) const;
    // Drops a placement into the level at run time (e.g. scripted changes). Solid furniture
    // makes its cell solid and is refused when the cell is out of bounds or already solid;
    // a prop only needs the cell to be in bounds.
    bool addFurniture(const Placement& p);
    // Removes the placement with this id (a prop picked up). False if there is none.
    bool removePlacement(const std::string& id);

    // Increases on every change, so renderers know when to rebuild geometry. Unique across all levels.
    uint64_t revision() const { return revision_; }

    // 4-connected BFS steps from (gx,gy) to every cell; -1 = unreachable/blocked.
    std::vector<int> distanceField(int gx, int gy) const;

    // Samples the segment for blocking glyphs and furniture.
    bool lineOfSight(double x0, double y0, double x1, double y1) const;

private:
    int w_ = 0;
    int h_ = 0;
    uint64_t revision_ = 0;
    std::vector<char> cells_;
    std::vector<char> solid_;  // 1 where furniture stands
    std::vector<Placement> furniture_;
    TileSetPtr tiles_ = defaultTileSet();
};

}  // namespace mythos
