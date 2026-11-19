#ifndef MYTHOS_ENGINE_EDITOR_LEVEL_DOC_H_
#define MYTHOS_ENGINE_EDITOR_LEVEL_DOC_H_
// A level file as the editor holds it: the glyph rows and the two placement
// lists, with the comment header kept. No SDL, GL or UI: it is unit-tested.

#include <string>
#include <vector>

namespace mythos::editor {

// One line of [furniture] (solid, one cell, off and lift 0) or [decor].
struct Item {
  int x = 0, y = 0;
  std::string model;
  double yaw = 0;               // degrees; 0 faces south (+z), 90 east
  double off_x = 0, off_z = 0;  // metres from the cell centre (decor)
  double lift = 0;              // metres above the floor (decor)
  bool operator==(const Item&) const = default;
};

struct LevelDoc {
  std::string name;                 // file stem: "house_1963"
  std::vector<std::string> header;  // comment lines before [map], without ';'
  std::vector<std::string> rows;
  std::vector<Item> furniture, decor;

  int Width() const {
    return rows.empty() ? 0 : static_cast<int>(rows[0].size());
  }
  int Height() const { return static_cast<int>(rows.size()); }
  bool InBounds(int x, int y) const {
    return y >= 0 && y < Height() && x >= 0 && x < Width();
  }
  char At(int x, int y) const { return InBounds(x, y) ? rows[y][x] : '#'; }
  // False when out of bounds.
  bool Set(int x, int y, char glyph);
  // Adds or removes whole rows and columns at the bottom / right edge.
  void Resize(int width, int height, char fill);

  bool operator==(const LevelDoc&) const = default;
};

// Parses level text (the format of mythos::Level::ParseText).
bool ParseLevelDoc(const std::string& text, LevelDoc* out, std::string* error);
// Text that mythos::Level parses back to the same level. A level the editor
// saved is stamped "map_editor" in its first lines, so tools/generate_assets.py
// leaves it alone.
std::string SerializeLevelDoc(const LevelDoc& doc);

// A number the way a level file writes it: at most 4 decimals, no trailing
// zeros.
std::string FormatNumber(double v);

}  // namespace mythos::editor

#endif  // MYTHOS_ENGINE_EDITOR_LEVEL_DOC_H_
