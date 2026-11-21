#ifndef MYTHOS_ENGINE_EDITOR_ADAPTER_H_
#define MYTHOS_ENGINE_EDITOR_ADAPTER_H_

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/json.h"
#include "engine/gfx/textures.h"

namespace mythos::editor {
// The editor's model of the engine's level files and the game's JSON data. A
// `DataSet` maps a data file name (as listed by `Adapter::DataFiles`, without
// directory or ".json") to its parsed contents.
//
// This header is the whole contract between the generic level editor and a
// game: the game derives from `Adapter` and the editor never names a game.
using DataSet = std::map<std::string, json::Value>;

// Entity kinds. The first two are built into the editor (the `[furniture]` and
// `[decor]` sections of a level); a game's own kinds start at `kFirstGameKind`.
constexpr int kFurniture = 0;
constexpr int kDecor = 1;
constexpr int kFirstGameKind = 16;

// Identifies one editable thing. Built-in kinds use `index` (position in the
// level's list); a game kind may use `index`, `id` or both. `index` is -1 when
// the reference is empty or invalid.
struct EntityRef {
  int kind = kFurniture;
  int index = -1;
  std::string id;

  bool operator==(const EntityRef&) const = default;
};

// Position of an entity in world units (x = column, z = row, y up) and its yaw
// in degrees (0 faces +z). `has_yaw` is false for things that cannot turn.
struct Pose {
  double x = 0, y = 0, z = 0, yaw = 0;
  bool has_yaw = false;
};

// Which level, and which game-defined context (a variant such as an era), is
// being edited.
struct Where {
  std::string level, context;
};

// How a level is grouped and stacked in the editor's level list.
struct LevelInfo {
  std::string group;
  int group_order = 0;
  int floor = 0;
  // True when the level is one floor of a vertical stack of levels drawn
  // together; `floor` is its position in that stack.
  bool stacked = false;
  bool outdoor = false;
  std::vector<std::string> contexts;
};

// How the viewport draws an entity that is not a plain model.
enum class Shape {
  kModel,
  kLight,
  kMarker,
  kActor,
  kRegion,
};

// What the viewport needs to draw and pick an entity. Which fields apply
// depends on `shape`: `model` for kModel, `radius` and `color` for kLight,
// `facing_deg` for kActor, and the x0..z1 rectangle (world units) for kRegion.
struct Visual {
  Shape shape = Shape::kModel;
  std::string model;
  float color[3] = {1.0f, 0.85f, 0.4f};
  float radius = 0;
  float facing_deg = 0;
  float x0 = 0, z0 = 0, x1 = 0, z1 = 0;
  // Whether the entity can be selected by clicking it in the viewport.
  bool pickable = true;
  // Whether `Project::Check` reports it when the entity stands inside a wall.
  bool wall_check = true;

  // Label of the height field in the Inspector, and whether x and z can be
  // edited.
  std::string y_label = "Lift";
  bool edit_xz = true;
};

// One finding of the level check. When `has_ref` is set, `ref` is the offending
// entity; (x, z) is where to show it.
struct Problem {
  std::string text;
  EntityRef ref;
  bool has_ref = false;
  double x = 0, z = 0;
};

// A setting the game offers in the editor's preview dialog, stored in
// `PreviewRequest::options` under `key`: a checkbox when `toggle` is true,
// otherwise an integer no lower than `min`. `value` is the default.
struct PreviewOption {
  std::string key, label;
  bool toggle = true;
  int value = 0, min = 0;
};

// What the editor asks of the game to render a preview image: the place and
// heading to look from, the chosen options by key, and where to write the image
// (a PPM file) and which assets to use.
struct PreviewRequest {
  Where where;
  float x = 0, z = 0, heading_deg = 0;
  std::map<std::string, int> options;
  std::string out_ppm;
  std::string asset_dir;
};

// Everything the editor needs to know about a game. The defaults describe a
// game with no data of its own, which is what the stock `MythosEditor` uses.
// All methods are called on the UI thread. Methods taking a `DataSet` may be
// given a copy to edit; `Project` marks the data dirty when a mutator returns
// success.
class Adapter {
 public:
  virtual ~Adapter() = default;

  // Application name shown in the editor window.
  [[nodiscard]] virtual std::string Name() const { return "Mythos"; }

  // Organization and application names that select the folder of the editor's
  // preferences file.
  [[nodiscard]] virtual std::string PrefsOrg() const { return "mythos"; }
  [[nodiscard]] virtual std::string PrefsApp() const { return "MythosEditor"; }

  // The JSON files under `assets/data/` the editor loads, edits and saves, by
  // name without extension.
  [[nodiscard]] virtual std::vector<std::string> DataFiles() const {
    return {};
  }

  // Registers the textures a level in this game can use (the common materials,
  // the stage's overrides and its model textures). The default is right for
  // most games.
  virtual void SetupTextures(TextureLibrary& lib,
                             const std::string& stage_dir) const;

  // Name of the level the game starts in, or empty when there is none.
  [[nodiscard]] virtual std::string StartLevel(const DataSet&) const {
    return "";
  }

  // Grouping and stacking of a level.
  [[nodiscard]] virtual LevelInfo Info(const DataSet&,
                                       const std::string&) const {
    return {};
  }

  // What the game calls its contexts (for the UI) and the list of them.
  [[nodiscard]] virtual std::string ContextName() const { return "Context"; }

  [[nodiscard]] virtual std::vector<std::string> Contexts(
      const DataSet&) const {
    return {};
  }

  // The levels drawn together with the one in `Where`, as (floor, level name)
  // pairs. Entries with an empty or unknown name are ignored.
  [[nodiscard]] virtual std::vector<std::pair<int, std::string>> Stack(
      const DataSet&, const Where&) const {
    return {};
  }

  // The game's own entities (kinds from `kFirstGameKind`) in a level and
  // context.
  [[nodiscard]] virtual std::vector<EntityRef> Entities(const DataSet&,
                                                        const Where&) const {
    return {};
  }

  // Plural display name of a game entity kind.
  [[nodiscard]] virtual std::string KindName(int) const { return "Things"; }

  // Fills in how to draw an entity. Returns false when the reference is
  // invalid.
  virtual bool Describe(const DataSet&, const Where&, const EntityRef&,
                        Visual*) const {
    return false;
  }

  // Reads the pose of an entity; false when it has none.
  virtual bool GetPose(const DataSet&, const Where&, const EntityRef&,
                       Pose*) const {
    return false;
  }

  // Moves an entity by editing the data; false when the entity cannot move.
  virtual bool SetPose(DataSet&, const Where&, const EntityRef&,
                       const Pose&) const {
    return false;
  }

  // Short human-readable name of an entity for lists and messages.
  [[nodiscard]] virtual std::string Label(const DataSet&, const Where&,
                                          const EntityRef&) const {
    return "?";
  }

  // The entity's own JSON object for the Inspector to edit in place, or null.
  // The pointer is valid until the data set is next restored.
  virtual json::Value* Data(DataSet&, const Where&, const EntityRef&) const {
    return nullptr;
  }

  // Copies an entity and returns the copy; an empty reference (index -1, no id)
  // means it could not be copied.
  virtual EntityRef Duplicate(DataSet&, const Where&,
                              const EntityRef& r) const {
    return {r.kind, -1, ""};
  }

  // Deletes an entity; false when it cannot be removed.
  virtual bool Remove(DataSet&, const Where&, const EntityRef&) const {
    return false;
  }

  // Appends the game's own findings for a level to the check results.
  virtual void Check(const DataSet&, const Where&,
                     std::vector<Problem>*) const {}

  // Names of the game binaries the preview may launch, tried in order, and the
  // options the preview dialog offers.
  [[nodiscard]] virtual std::vector<std::string> GameExecutables() const {
    return {};
  }

  [[nodiscard]] virtual std::vector<PreviewOption> PreviewOptions() const {
    return {};
  }

  // Builds the command line that renders `request` into its `out_ppm`, and
  // stores it in the vector. The data set is a private copy the game may edit
  // (for example to move the start position). Returns false when no preview is
  // possible.
  virtual bool Preview(DataSet&, const PreviewRequest&,
                       std::vector<std::string>*) const {
    return false;
  }
};

// Registers a texture for every .bmp in `dir` that the library does not know
// yet (gray placeholders, since the editor draws models flat), then applies the
// image overrides found there. Returns how many were added.
int RegisterModelTextures(TextureLibrary& lib, const std::string& dir);
}

#endif
