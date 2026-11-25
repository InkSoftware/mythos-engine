#ifndef MYTHOS_ENGINE_EDITOR_LEVEL_DOC_H_
#define MYTHOS_ENGINE_EDITOR_LEVEL_DOC_H_

#include <string>
#include <vector>

#include "engine/core/events.h"
#include "engine/world/decal.h"
#include "engine/world/trigger.h"

namespace mythos::editor {
// One `[furniture]` or `[decor]` line of a level: the cell (x = column, y =
// row), model name and yaw in degrees. The offsets (world units from the cell
// center) and `lift` (height above the floor) belong to decor and, optionally,
// to furniture (a piece pushed against the wall behind it).
struct Item {
  int x = 0, y = 0;
  std::string model;
  double yaw = 0;
  double off_x = 0, off_z = 0;
  double lift = 0;
  // The id of the prefab link that placed it (`[links]`; written as a trailing
  // `@id` token the game's loader ignores); empty for an ordinary item.
  std::string link;

  bool operator==(const Item&) const = default;
};

// A prefab placed as a link: the file it came from and where, so it can be
// updated when the prefab changes (see editor/prefab.h). What it placed is
// tagged: items and decals by `link`, triggers and rules by an id that starts
// with `<id>/`.
struct PrefabLink {
  std::string id;
  std::string prefab;
  int x = 0, y = 0, turns = 0;

  bool operator==(const PrefabLink&) const = default;
};

// An editable level file: the `[map]` glyph rows (all the same width) and the
// item and decal lists. `header` keeps the leading `;` comment lines so saving
// does not lose them.
struct LevelDoc {
  std::string name;
  std::vector<std::string> header;
  std::vector<std::string> rows;
  std::vector<Item> furniture, decor;
  std::vector<Decal> decals;
  std::vector<Trigger> triggers;
  std::vector<EventRule> rules;
  std::vector<PrefabLink> links;

  [[nodiscard]] int Width() const {
    return rows.empty() ? 0 : static_cast<int>(rows[0].size());
  }

  [[nodiscard]] int Height() const { return static_cast<int>(rows.size()); }

  [[nodiscard]] bool InBounds(const int x, const int y) const {
    return y >= 0 && y < Height() && x >= 0 && x < Width();
  }

  // The glyph at a cell; cells outside the map read as wall ('#').
  [[nodiscard]] char At(const int x, const int y) const {
    return InBounds(x, y) ? rows[y][x] : '#';
  }

  // Sets a cell; returns false when it is outside the map.
  bool Set(int x, int y, char glyph);

  // Crops or grows the map, filling new cells with `fill` and dropping items
  // decals and cell triggers that end up outside. Ignores sizes below 1.
  void Resize(int width, int height, char fill);

  bool operator==(const LevelDoc&) const = default;
};

// Parses level file text. Returns false and describes the problem in `error`
// (when non-null) on a malformed line or rows of unequal length; `out` is
// untouched then.
bool ParseLevelDoc(const std::string& text, LevelDoc* out, std::string* error);

// Writes the level file text, stamping a header line when the file does not
// already carry one from the map editor. Returns empty for invalid, duplicate,
// or out-of-bounds cell triggers, or invalid or duplicate rules.
std::string SerializeLevelDoc(const LevelDoc& doc);

// Renames a trigger and the rules driven by it (a rule whose event is that
// trigger). Returns false, changing nothing, if `from` does not exist, `to`
// is empty or already names another trigger. `rules_updated` (when non-null)
// gets the number of rules changed.
bool RenameTrigger(LevelDoc& doc, const std::string& from,
                   const std::string& to, int* rules_updated = nullptr);

// Removes a trigger and the rules driven by it, which could never fire again.
// Returns how many rules went, or -1 if there is no such trigger.
int RemoveTrigger(LevelDoc& doc, const std::string& id);

// A rule id not used yet, "rule_1", "rule_2", ...
std::string NewRuleId(const LevelDoc& doc);

// Formats a number with at most four decimals and no trailing zeros, so saved
// files stay stable and readable.
std::string FormatNumber(double v);
}

#endif
