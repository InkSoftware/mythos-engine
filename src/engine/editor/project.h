#ifndef MYTHOS_ENGINE_EDITOR_PROJECT_H_
#define MYTHOS_ENGINE_EDITOR_PROJECT_H_
// What the editor edits: the level files of assets/levels, the tile legend,
// and the JSON files a game's Adapter works on. No SDL, GL or UI: the rules are
// unit-tested.

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

// Everything undo has to bring back.
struct Snapshot {
  std::map<std::string, LevelDoc> levels;
  DataSet data;
};

class Project {
 public:
  // The adapter (not owned) must outlive the project; null edits levels only.
  explicit Project(const Adapter* adapter = nullptr);

  // root is the folder holding assets/ (the game's repository). False with a
  // message when a file cannot be read.
  bool Load(const std::string& root, std::string* error);
  // Writes every changed file. Levels are written as the editor's text, the
  // JSON files with two-space indentation.
  bool Save(std::string* error);

  const Adapter& adapter() const { return *adapter_; }
  const std::string& root() const { return root_; }
  std::string AssetDir() const { return root_ + "/assets"; }

  // ---- levels
  std::vector<std::string> LevelNames() const;
  LevelDoc* Level(const std::string& name);
  const LevelDoc* Level(const std::string& name) const;
  LevelInfo Info(const std::string& name) const {
    return adapter_->Info(data, name);
  }
  std::vector<std::string> Contexts() const { return adapter_->Contexts(data); }
  // The floors of a context, as {floor, level name}, with `level` standing in
  // for its own floor and only the levels that exist: the neighbours drawn as
  // ghosts. Empty for a level that is not stacked.
  std::vector<std::pair<int, std::string>> Stack(
      const std::string& context, const std::string& level) const;
  // A new level of w x h cells, walled round. False when the name is taken or
  // not a plain file name.
  bool CreateLevel(const std::string& name, int w, int h);
  const TileSet& Tiles() const { return *tiles_; }
  const TileSetPtr& TilesPtr() const { return tiles_; }
  // The glyphs the legend names, plus the plain floor, for the palette.
  std::vector<char> Glyphs() const;

  // ---- entities on a level: furniture and decor, then the adapter's
  std::vector<EntityRef> Entities(const Where& w) const;
  // The JSON object behind a game's thing, for the Inspector; null for
  // furniture and decor.
  json::Value* Data(const Where& w, const EntityRef& r);
  bool Describe(const Where& w, const EntityRef& r, Visual* out) const;
  bool GetPose(const Where& w, const EntityRef& r, Pose* out) const;
  // Moves an entity. Furniture and decor stay on the level's cells.
  bool SetPose(const Where& w, const EntityRef& r, const Pose& p);
  // A readable name for the outliner.
  std::string Label(const Where& w, const EntityRef& r) const;
  std::string KindName(int kind) const;
  // The copy's reference (index < 0 when it cannot be copied).
  EntityRef Duplicate(const Where& w, const EntityRef& r);
  bool Remove(const Where& w, const EntityRef& r);
  EntityRef AddFurniture(const std::string& level, const Item& item);
  EntityRef AddDecor(const std::string& level, const Item& item);

  // Furniture in walls, things outside the map, missing models, a stairwell
  // with no hole above or below, and what the adapter adds.
  std::vector<Problem> Check(const Where& w,
                             const std::vector<std::string>& model_names) const;

  // ---- changes
  void Touch(const std::string& level);  // a level changed
  void TouchData();                      // the data files changed
  unsigned Revision() const { return revision_; }
  bool Dirty() const { return !dirty_levels_.empty() || dirty_data_; }
  bool IsDirty(const std::string& level) const {
    return dirty_levels_.contains(level);
  }
  Snapshot Take() const;
  void Restore(const Snapshot& s);

  DataSet data;  // the adapter's files, edited in place

 private:
  const Adapter* adapter_;
  std::string root_;
  std::map<std::string, LevelDoc> levels_;
  TileSetPtr tiles_;
  std::set<std::string> dirty_levels_;
  bool dirty_data_ = false;
  unsigned revision_ = 1;
};

}  // namespace mythos::editor

#endif  // MYTHOS_ENGINE_EDITOR_PROJECT_H_
