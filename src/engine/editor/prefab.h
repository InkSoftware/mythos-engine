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
  // Decals (cell relative), triggers (a cell trigger's cell is relative; a
  // box's bounds are world units from the top-left corner) and the rules
  // driven by those triggers.
  std::vector<Decal> decals;
  std::vector<Trigger> triggers;
  std::vector<EventRule> rules;

  bool operator==(const Prefab&) const = default;
};

// Parses prefab text (`;` comments and blank lines allowed). Returns false
// with the reason in `error`; `out` is untouched then. An item outside the
// footprint is an error, and a missing `[size]` is taken from the items.
bool ParsePrefab(const std::string& text, Prefab* out, std::string* error);
std::string SerializePrefab(const Prefab& prefab);

// The furniture, decor, decals, triggers (a cell trigger in the rectangle, or a
// box inside it) and the rules those triggers drive, in cells x0..x1, y0..y1
// (inclusive, any corner order) of `doc`, relative to the top-left corner of
// that rectangle. The footprint is the rectangle clamped to the map; empty if
// it is outside.
Prefab CapturePrefab(const LevelDoc& doc, int x0, int y0, int x1, int y1,
                     const std::string& name);

// The prefab turned `quarter_turns` times clockwise as seen on the map (north
// up), negative turns counter-clockwise: cells, the footprint, yaw and offsets.
Prefab Rotated(const Prefab& prefab, int quarter_turns);

struct PlaceResult {
  int furniture = 0, decor = 0, decals = 0, triggers = 0, rules = 0;
  // Items left out: outside the map, on a cell `blocked` refuses (furniture),
  // or on a cell that already holds furniture.
  int skipped = 0;
};

// Adds the prefab's items to `doc` with its top-left corner at (x, y), turned
// `quarter_turns` times. `blocked(glyph)` (optional) says which map glyphs
// furniture may not stand on (walls). Decor and decals only have to be inside
// the map. Triggers keep their id unless the level has one of that name, then
// get `_2`, `_3`...; their rules follow (new rule ids, the rule's trigger
// renamed with it).
//
// With a non-empty `link_id` the placement is a *link*: the doc records a
// `PrefabLink` (id, the prefab's name, where, turned how), what was placed is
// tagged (`Item::link`, `Decal::link`, trigger and rule ids `<id>/<name>`),
// and `UpdateLinked` can later place the prefab's new version over it.
PlaceResult PlacePrefab(LevelDoc& doc, const Prefab& prefab, int x, int y,
                        int quarter_turns = 0,
                        const std::function<bool(char)>& blocked = {},
                        const std::string& link_id = {});

// A link id not used yet: "link_1", "link_2"...
std::string NewLinkId(const LevelDoc& doc);

// Removes everything a link placed (tagged items and decals, triggers and
// rules with the id prefix) and the link record; returns the number of things
// removed, or -1 without such a link.
int RemoveLinked(LevelDoc& doc, const std::string& link_id);

// Removes what the link placed and places `prefab` (its new version) again
// where and how the link says. Edits made to the linked items are replaced;
// `Unlink` first to keep them. Returns false without such a link.
bool UpdateLinked(LevelDoc& doc, const std::string& link_id,
                  const Prefab& prefab,
                  const std::function<bool(char)>& blocked = {},
                  PlaceResult* result = nullptr);

// Turns the link's items back into ordinary level items (drops the tags and
// the record; the ids of its triggers and rules stay as they are). Returns
// false without such a link.
bool Unlink(LevelDoc& doc, const std::string& link_id);

// A file-safe name: letters, digits, '_' and '-', anything else becomes '_'
// (empty gives "prefab").
std::string SafePrefabName(const std::string& name);
}

#endif
