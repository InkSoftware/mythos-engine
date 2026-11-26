#ifndef MYTHOS_ENGINE_WORLD_LEVEL_H_
#define MYTHOS_ENGINE_WORLD_LEVEL_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/events.h"
#include "engine/world/decal.h"
#include "engine/world/tiles.h"
#include "engine/world/trigger.h"

// The grid world: glyph map, furniture placements and the collision tests built
// on them. One cell is one world unit; world x is the column, z is the row.
namespace mythos {
// A model placed in a cell: either solid furniture or non-solid decor.
struct Placement {
  // Cell column and row (the row is world z).
  int x = 0;
  int y = 0;
  std::string model;
  // Rotation in degrees; 0 faces +z, 90 faces +x.
  double yaw_deg = 0;
  // Offset from the cell center in meters, and height above the floor.
  double off_x = 0, off_z = 0;
  double lift = 0;
  // Solid placements make their cell impassable.
  bool solid = true;
  // Optional unique id; RemovePlacement needs one.
  std::string id;
  // A footprint (half sizes in metres along the piece's own x and z; 0 = the
  // whole cell): a solid piece then blocks only that box of its cell
  // (BoxBlocked). Passable stays a whole-cell test.
  double half_x = 0, half_z = 0;
};

// A rectangular map of glyphs plus placements and decals. Revision() changes
// on edits so caches can detect them. Not thread-safe.
class Level {
 public:
  static constexpr std::size_t kMaxDecals = 1024;

  // Loads a map from equal-length rows, clearing furniture, decals, triggers
  // and rules. Returns false and sets *error for an empty map or ragged rows.
  bool Parse(const std::vector<std::string>& rows,
             std::string* error = nullptr);

  // Parses level text: a "[map]" section of rows, "[furniture]" lines ("x y
  // model yaw") and "[decor]" lines ("x y model yaw [offX offZ lift]").
  // [decals], [triggers] and [rules] use the formats in their respective
  // headers. Cell triggers must be in bounds; trigger and rule IDs must be
  // unique. Blank lines and lines starting with ';' are ignored. On placement,
  // decal or trigger bounds failure, the map is already replaced but those
  // objects are not kept.
  bool ParseText(const std::string& text, std::string* error = nullptr);

  // Reads path through the virtual file system and parses it with ParseText.
  bool LoadFile(const std::string& path, std::string* error = nullptr);

  // Sets the tile set; null selects the default one.
  void SetTiles(const TileSetPtr& tiles) {
    tiles_ = tiles ? tiles : DefaultTileSet();
  }

  [[nodiscard]] const TileSet& Tiles() const { return *tiles_; }
  [[nodiscard]] const TileSetPtr& TilesPtr() const { return tiles_; }

  [[nodiscard]] int Width() const { return w_; }
  [[nodiscard]] int Height() const { return h_; }

  [[nodiscard]] bool InBounds(const int x, const int y) const {
    return x >= 0 && y >= 0 && x < w_ && y < h_;
  }

  // The glyph at a cell; out of bounds reads as '#'.
  [[nodiscard]] char At(int x, int y) const;

  // Replaces a glyph and bumps the revision. Out of bounds is ignored.
  void Set(int x, int y, char glyph);

  // Whole-cell test: in bounds, not a solid glyph and without solid furniture.
  // Thin walls count as impassable here; use BoxBlocked for the precise test.
  [[nodiscard]] bool Passable(int x, int y) const;

  // An axis-aligned rectangle in world units (x and z).
  struct Rect {
    double x0, z0, x1, z1;
  };

  // The rectangles of a thin wall cell: the central post plus an arm toward
  // each solid or door neighbor. Empty for any other cell. The mesher and
  // collision both use this, so what is drawn is what blocks.
  [[nodiscard]] std::vector<Rect> ThinRects(int x, int y) const;

  // Bit mask of the neighbors a thin wall joins: 1 north (z-1), 2 south (z+1),
  // 4 west (x-1), 8 east (x+1).
  [[nodiscard]] unsigned ThinArms(int x, int y) const;

  // Precise collision test for the box [min_x, max_x] x [min_z, max_z] in world
  // units, honoring thin wall geometry. Outside the map is blocked.
  [[nodiscard]] bool BoxBlocked(double min_x, double min_z, double max_x,
                                double max_z) const;

  // BoxBlocked for a single point.
  [[nodiscard]] bool PointBlocked(double x, double z) const;

  // Finds the first cell (row-major) holding glyph; returns false if none.
  bool Find(char glyph, int& x, int& y) const;

  // All cells holding the glyph as (column, row), in row-major order.
  [[nodiscard]] std::vector<std::pair<int, int>> FindAll(char c) const;

  [[nodiscard]] const std::vector<Placement>& Furniture() const {
    return furniture_;
  }

  // The solid placement in a cell, or null.
  [[nodiscard]] const Placement* FurnitureAt(int x, int y) const;

  // Adds a placement. Fails if out of bounds or a solid one targets an
  // impassable cell.
  bool AddFurniture(const Placement& p);

  [[nodiscard]] const std::vector<Decal>& Decals() const { return decals_; }

  // Invalid decals are rejected; at capacity the oldest decal is evicted.
  bool AddDecal(const Decal& decal);

  [[nodiscard]] const std::vector<Trigger>& Triggers() const {
    return triggers_;
  }
  // Rejects invalid or duplicate triggers and cells outside the map.
  bool AddTrigger(const Trigger& trigger);
  bool RemoveTrigger(const std::string& id);

  [[nodiscard]] const std::vector<EventRule>& Rules() const { return rules_; }

  // Removes the placement with the given id (empty ids never match); returns
  // whether one was found.
  bool RemovePlacement(const std::string& id);

  // Process-unique stamp, renewed on every change to map contents.
  [[nodiscard]] uint64_t Revision() const { return revision_; }

  // True if no point sampled along the segment (x0, y0)-(x1, y1) in the floor
  // plane is blocked. The end points themselves are not tested.
  [[nodiscard]] bool LineOfSight(double x0, double y0, double x1,
                                 double y1) const;

 private:
  int w_ = 0;
  int h_ = 0;
  uint64_t revision_ = 0;
  std::vector<char> cells_;
  // Per-cell flag: 1 if solid furniture stands there.
  std::vector<char> solid_;
  std::vector<Placement> furniture_;
  std::vector<Decal> decals_;
  std::vector<Trigger> triggers_;
  std::vector<EventRule> rules_;
  TileSetPtr tiles_ = DefaultTileSet();
};
}

#endif
