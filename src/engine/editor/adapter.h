#ifndef MYTHOS_ENGINE_EDITOR_ADAPTER_H_
#define MYTHOS_ENGINE_EDITOR_ADAPTER_H_
// What a game teaches the level editor. The editor itself knows the engine's
// formats: the level files (assets/levels/*.txt, with their [furniture] and
// [decor] lists) and the tile legend (assets/data/tiles.json). Everything a
// game adds on top, such as props or lamps kept in its own JSON files, which
// levels belong to which scenario, and how to run the game for a preview, comes
// through an Adapter. A game derives from it and passes it to RunEditor(); with
// no adapter the editor edits levels, furniture and decor only.
//
// An adapter is pure logic over JSON, so it is unit-tested without a window.
// No SDL, GL or UI here.

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/json.h"
#include "engine/gfx/textures.h"

namespace mythos::editor {

// The game's JSON files, by name: "props" is assets/data/props.json. The
// editor loads and saves them (two-space indent, a final newline), keeps them
// in the undo history and tracks whether they changed; the adapter reads and
// edits the values.
using DataSet = std::map<std::string, json::Value>;

// A thing on a level. kind 0 and 1 are the level file's own lines; a game's
// kinds start at kFirstGameKind.
constexpr int kFurniture = 0;  // a [furniture] line (solid, fills one cell)
constexpr int kDecor = 1;      // a [decor] line (loose, off the cell grid)
constexpr int kFirstGameKind = 16;

struct EntityRef {
  int kind = kFurniture;
  int index = -1;  // position in its list
  std::string id;  // a key, for things a game stores by name
  bool operator==(const EntityRef&) const = default;
};

// Where an entity stands: world x / z on the map plane, y up (metres above the
// floor), yaw in degrees (the level files' convention: 0 south, 90 east).
struct Pose {
  double x = 0, y = 0, z = 0, yaw = 0;
  bool has_yaw = false;
};

// What the editor is showing: a level, and the game's context for it (a
// scenario such as an era or a chapter that decides which lamps, rooms and
// floors belong to the level; "" when a game has none).
struct Where {
  std::string level, context;
};

// Where a level sits in the game.
struct LevelInfo {
  std::string group;     // the Levels panel heading ("" = all in one list)
  int group_order = 0;   // headings are listed by this, then by name
  int floor = 0;         // its place in a stack of floors
  bool stacked = false;  // floors are drawn one above the other (kFloorPitch)
  bool outdoor = false;  // no ceiling, a sky
  std::vector<std::string> contexts;  // the contexts that use this level
};

// How the viewport draws a thing the game places.
enum class Shape {
  kModel,   // a model standing at its pose
  kLight,   // a cross with a stem to the floor; the radius shows when picked
  kMarker,  // a small circle
  kActor,   // a circle and an arrow along facing_deg (a spawn point)
  kRegion,  // a box over map cells; moved in the Inspector or by its pose
};
struct Visual {
  Shape shape = Shape::kModel;
  std::string model;  // kModel
  float color[3] = {1.0f, 0.85f, 0.4f};
  float radius = 0;      // kLight
  float facing_deg = 0;  // kActor: the arrow points along (cos, sin)
  float x0 = 0, z0 = 0, x1 = 0, z1 = 0;  // kRegion, in world units
  bool pickable = true;                  // false: only the Outliner selects it
  bool wall_check = true;  // Problems: flag it standing inside a wall
  // The Inspector's fields for the pose: the label of the height ("" = none)
  // and whether x / z are edited there (a region is edited as data).
  std::string y_label = "Lift";
  bool edit_xz = true;
};

// A thing the Problems list flags.
struct Problem {
  std::string text;
  EntityRef ref;
  bool has_ref = false;
  double x = 0, z = 0;  // where to look
};

// An option of the game preview (a checkbox, or a number with a minimum).
struct PreviewOption {
  std::string key, label;
  bool toggle = true;
  int value = 0, min = 0;
};
struct PreviewRequest {
  Where where;
  float x = 0, z = 0, heading_deg = 0;  // the viewport camera's target
  std::map<std::string, int> options;   // PreviewOption::key -> value
  std::string out_ppm;                  // where the game must write the picture
  std::string asset_dir;                // the assets to run on
};

class Adapter {
 public:
  virtual ~Adapter() = default;

  // The game's name, for the window title.
  virtual std::string Name() const { return "Mythos"; }
  // Preferences live in <org>/<app> (the editor's layout file).
  virtual std::string PrefsOrg() const { return "mythos"; }
  virtual std::string PrefsApp() const { return "MythosEditor"; }

  // The JSON files of assets/data the game's entities live in, by name without
  // ".json". Each must exist.
  virtual std::vector<std::string> DataFiles() const { return {}; }

  // Textures the viewport draws with: the game's procedural art, then files
  // of stage_dir over it, then stage_dir/modeltex. The default registers the
  // engine's common materials.
  virtual void SetupTextures(TextureLibrary& lib,
                             const std::string& stage_dir) const;

  // ---- levels
  virtual std::string StartLevel(const DataSet&) const { return ""; }
  virtual LevelInfo Info(const DataSet&, const std::string& /*level*/) const {
    return {};
  }
  // The name the context list is shown under ("Era").
  virtual std::string ContextName() const { return "Context"; }
  virtual std::vector<std::string> Contexts(const DataSet&) const { return {}; }
  // The floors of a context as {floor, level name}, in order, for the ghosts
  // above and below and for the stairwell check. Return empty for a level
  // that is not stacked.
  virtual std::vector<std::pair<int, std::string>> Stack(const DataSet&,
                                                         const Where&) const {
    return {};
  }

  // ---- things on a level (besides furniture and decor)
  virtual std::vector<EntityRef> Entities(const DataSet&, const Where&) const {
    return {};
  }
  // The heading of a kind in the Outliner.
  virtual std::string KindName(int /*kind*/) const { return "Things"; }
  virtual bool Describe(const DataSet&, const Where&, const EntityRef&,
                        Visual*) const {
    return false;
  }
  virtual bool GetPose(const DataSet&, const Where&, const EntityRef&,
                       Pose*) const {
    return false;
  }
  // Moves it; false when the thing is gone.
  virtual bool SetPose(DataSet&, const Where&, const EntityRef&,
                       const Pose&) const {
    return false;
  }
  virtual std::string Label(const DataSet&, const Where&,
                            const EntityRef&) const {
    return "?";
  }
  // The JSON object the Inspector edits, or null.
  virtual json::Value* Data(DataSet&, const Where&, const EntityRef&) const {
    return nullptr;
  }
  // A copy of it, index < 0 when it cannot be copied.
  virtual EntityRef Duplicate(DataSet&, const Where&,
                              const EntityRef& r) const {
    return {r.kind, -1, ""};
  }
  // False when it cannot be deleted (a prop that carries story text).
  virtual bool Remove(DataSet&, const Where&, const EntityRef&) const {
    return false;
  }
  // Problems of the game's own, added to the editor's.
  virtual void Check(const DataSet&, const Where&,
                     std::vector<Problem>* /*out*/) const {}

  // ---- game preview
  // Where the game's executable may be, relative to the editor's folder; none
  // turns the preview off.
  virtual std::vector<std::string> GameExecutables() const { return {}; }
  virtual std::vector<PreviewOption> PreviewOptions() const { return {}; }
  // The command line of the game's own screenshot mode, without the
  // executable. data is a copy the adapter may edit (it is written to the
  // temporary assets the game runs on): for instance, to make the open level
  // the one the game loads.
  virtual bool Preview(DataSet& /*data*/, const PreviewRequest&,
                       std::vector<std::string>* /*args*/) const {
    return false;
  }
};

// Registers every BMP of dir as a texture named after the file, and loads it:
// the textures of a game's models. Returns how many were registered.
int RegisterModelTextures(TextureLibrary& lib, const std::string& dir);

}  // namespace mythos::editor

#endif  // MYTHOS_ENGINE_EDITOR_ADAPTER_H_
