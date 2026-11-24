// Prefabs: a reusable group of furniture and decor (a table with its chairs
// and a lamp), kept as a file and placed anywhere in a level, in any of the
// four orientations. The file mirrors the level format, so a game needs no
// code: `[size]`, `[furniture]` and `[decor]` sections with the same lines as
// a level, the cells relative to the group's top-left corner. Placed items are
// ordinary level items, so anything can then be edited ("overridden") in the
// level without touching the prefab.
#ifndef MYTHOS_ENGINE_EDITOR_PREFAB_H_
#define MYTHOS_ENGINE_EDITOR_PREFAB_H_

#include <functional>
#include <string>
#include <vector>

#include "engine/editor/level_doc.h"

namespace mythos::editor {
struct Prefab {
  std::string name;
  // The footprint in cells (at least 1 x 1): what turning pivots on.
  int width = 1, height = 1;
  // Cells are relative to the top-left corner, 0 .. width-1 and 0 .. height-1.
  std::vector<Item> furniture, decor;

  bool operator==(const Prefab&) const = default;
};

// Parses prefab text (`;` comments and blank lines allowed). Returns false
// with the reason in `error`; `out` is untouched then. An item outside the
// footprint is an error, and a missing `[size]` is taken from the items.
bool ParsePrefab(const std::string& text, Prefab* out, std::string* error);
std::string SerializePrefab(const Prefab& prefab);

// The furniture and decor in cells x0..x1, y0..y1 (inclusive, any corner
// order) of `doc`, relative to the top-left corner of that rectangle. The
// footprint is the rectangle clamped to the map; empty if it is outside.
Prefab CapturePrefab(const LevelDoc& doc, int x0, int y0, int x1, int y1,
                     const std::string& name);

// The prefab turned `quarter_turns` times clockwise as seen on the map (north
// up), negative turns counter-clockwise: cells, the footprint, yaw and offsets.
Prefab Rotated(const Prefab& prefab, int quarter_turns);

struct PlaceResult {
  int furniture = 0, decor = 0;
  // Items left out: outside the map, on a cell `blocked` refuses (furniture),
  // or on a cell that already holds furniture.
  int skipped = 0;
};

// Adds the prefab's items to `doc` with its top-left corner at (x, y), turned
// `quarter_turns` times. `blocked(glyph)` (optional) says which map glyphs
// furniture may not stand on (walls). Decor only has to be inside the map.
PlaceResult PlacePrefab(LevelDoc& doc, const Prefab& prefab, int x, int y,
                        int quarter_turns = 0,
                        const std::function<bool(char)>& blocked = {});

// A file-safe name: letters, digits, '_' and '-', anything else becomes '_'
// (empty gives "prefab").
std::string SafePrefabName(const std::string& name);
}

#endif
