#ifndef MYTHOS_ENGINE_EDITOR_PROJECT_H_
#define MYTHOS_ENGINE_EDITOR_PROJECT_H_

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/json.h"
#include "engine/editor/adapter.h"
#include "engine/editor/level_doc.h"
#include "engine/world/tiles.h"

namespace mythos::editor {
// A full copy of the editable state, for undo and redo.
struct Snapshot {
  std::map<std::string, LevelDoc> levels;
  DataSet data;
};

// The open project: every level file and the game's JSON data under
// `<root>/assets`, plus which of them have unsaved changes. The built-in kinds
// (furniture, decor) are handled here; any other entity goes through the
// `Adapter`. Every mutator bumps `Revision()` so that `History` can tell
// something changed. With no adapter, a default one (no game data) is used.
class Project {
 public:
  explicit Project(const Adapter* adapter = nullptr);

  // Reads every level, `data/tiles.json` and the adapter's data files from
  // `<root>/assets`. Returns false and sets `error` (when non-null) on the
  // first failure; the project is then not usable. Clears all dirty marks.
  bool Load(const std::string& root, std::string* error);

  // Writes the dirty levels and data files, each atomically, and clears the
  // dirty marks. Data files whose text is unchanged on disk are left alone.
  // Returns false and sets `error` on the first write failure.
  bool Save(std::string* error);

  [[nodiscard]] const Adapter& adapter() const { return *adapter_; }
  [[nodiscard]] const std::string& root() const { return root_; }
  [[nodiscard]] std::string AssetDir() const { return root_ + "/assets"; }

  [[nodiscard]] std::vector<std::string> LevelNames() const;

  // A level by name, or null when there is none. Editing it directly does not
  // mark it dirty; call `Touch`.
  LevelDoc* Level(const std::string& name);

  [[nodiscard]] const LevelDoc* Level(const std::string& name) const;

  [[nodiscard]] LevelInfo Info(const std::string& name) const {
    return adapter_->Info(data, name);
  }

  [[nodiscard]] std::vector<std::string> Contexts() const {
    return adapter_->Contexts(data);
  }

  // The levels shown together with `level` as (floor, name) pairs sorted by
  // floor. `level` itself is always in the result when it is stacked.
  [[nodiscard]] std::vector<std::pair<int, std::string>> Stack(
      const std::string& context, const std::string& level) const;

  // Adds an empty level with a wall border. Fails when the name is empty, taken
  // or not alphanumeric / underscore, or the size is outside 3..200 on either
  // side.
  bool CreateLevel(const std::string& name, int w, int h);

  [[nodiscard]] const TileSet& Tiles() const { return *tiles_; }
  [[nodiscard]] const TileSetPtr& TilesPtr() const { return tiles_; }

  // The glyphs of the tile legend worth offering in the palette: floor ('.')
  // first, then every printable glyph the tile set gives a meaning.
  [[nodiscard]] std::vector<char> Glyphs() const;

  // All entities of a level: furniture, then decor, then the adapter's.
  [[nodiscard]] std::vector<EntityRef> Entities(const Where& w) const;

  // The JSON of a game entity for the Inspector; null for the built-in kinds.
  // Call `TouchData` after editing it.
  json::Value* Data(const Where& w, const EntityRef& r);

  bool Describe(const Where& w, const EntityRef& r, Visual* out) const;

  // Pose of an entity in world units. Furniture sits at its cell center; decor
  // adds its offsets and lift.
  bool GetPose(const Where& w, const EntityRef& r, Pose* out) const;

  // Moves an entity and marks what it edited dirty. Furniture snaps to the cell
  // under (x, z) clamped to the map; decor keeps the fraction as an offset
  // (rounded to 1 mm) and `y` as lift. Yaw is normalized to [0, 360).
  bool SetPose(const Where& w, const EntityRef& r, const Pose& p);

  [[nodiscard]] std::string Label(const Where& w, const EntityRef& r) const;

  [[nodiscard]] std::string KindName(int kind) const;

  // Copies an entity. Furniture goes to the next free walkable cell to the
  // right, decor is nudged 0.25 m. Returns a reference with index -1 on
  // failure.
  EntityRef Duplicate(const Where& w, const EntityRef& r);

  bool Remove(const Where& w, const EntityRef& r);

  // Appends an item (furniture ignores the decor offsets). Returns its
  // reference, with index -1 when the level does not exist.
  EntityRef AddFurniture(const std::string& level, const Item& item);

  EntityRef AddDecor(const std::string& level, const Item& item);

  // Lists what is wrong in a level: items outside the map or inside a wall,
  // models missing from `model_names`, stairwells and stair holes that do not
  // line up with the floor above or below, and the adapter's own findings.
  [[nodiscard]] std::vector<Problem> Check(
      const Where& w, const std::vector<std::string>& model_names) const;

  // Marks a level (or the data files) as changed: dirty for saving, and a new
  // revision.
  void Touch(const std::string& level);

  void TouchData();

  [[nodiscard]] unsigned Revision() const { return revision_; }

  [[nodiscard]] bool Dirty() const {
    return !dirty_levels_.empty() || dirty_data_;
  }

  [[nodiscard]] bool IsDirty(const std::string& level) const {
    return dirty_levels_.contains(level);
  }

  // Copies the editable state; `Restore` puts such a copy back, marking dirty
  // whatever differs from the current state.
  [[nodiscard]] Snapshot Take() const;

  void Restore(const Snapshot& s);

  // The adapter's data files, public so panels can edit them in place (then
  // `TouchData`).
  DataSet data;

 private:
  const Adapter* adapter_;
  std::string root_;
  std::map<std::string, LevelDoc> levels_;
  TileSetPtr tiles_;
  std::set<std::string> dirty_levels_;
  bool dirty_data_ = false;
  unsigned revision_ = 1;
};
}

#endif
