#ifndef MYTHOS_ENGINE_EDITOR_LEVEL_DOC_H_
#define MYTHOS_ENGINE_EDITOR_LEVEL_DOC_H_

#include <string>
#include <vector>

#include "engine/world/decal.h"

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

  bool operator==(const Item&) const = default;
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
  // and decals that end up outside. Ignores sizes below 1.
  void Resize(int width, int height, char fill);

  bool operator==(const LevelDoc&) const = default;
};

// Parses level file text. Returns false and describes the problem in `error`
// (when non-null) on a malformed line or rows of unequal length; `out` is
// untouched then.
bool ParseLevelDoc(const std::string& text, LevelDoc* out, std::string* error);

// Writes the level file text, stamping a header line when the file does not
// already carry one from the map editor.
std::string SerializeLevelDoc(const LevelDoc& doc);

// Formats a number with at most four decimals and no trailing zeros, so saved
// files stay stable and readable.
std::string FormatNumber(double v);
}

#endif
