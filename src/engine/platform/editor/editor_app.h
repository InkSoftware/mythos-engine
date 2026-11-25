#ifndef MYTHOS_ENGINE_PLATFORM_EDITOR_EDITOR_APP_H_
#define MYTHOS_ENGINE_PLATFORM_EDITOR_EDITOR_APP_H_

#include <deque>
#include <future>
#include <map>
#include <string>
#include <vector>

#include "engine/editor/adapter.h"
#include "engine/editor/history.h"
#include "engine/editor/prefab.h"
#include "engine/editor/project.h"
#include "engine/gfx/model.h"
#include "engine/gfx/textures.h"
#include "engine/platform/editor/viewport.h"
#include "engine/world/cutscene.h"
#include "imgui.h"

namespace mythos::editor {
// The editing tools of the viewport (keys Q, W, E, B, N).
enum class Tool { kSelect, kMove, kRotate, kPaint, kPlace, kPrefab };

// Startup options, filled from the command line by RunEditor.
struct EditorArgs {
  // Folder of the game project (holds assets/levels).
  std::string project_dir;
  // Folder of the textures and models the editor loads.
  std::string stage_dir;
  // Game binary for the preview; empty to look beside the editor.
  std::string game_exe;
  // Level to open first; empty for the adapter's start level.
  std::string level;
  // Entity to select at start, as "furniture:N", "decor:N" or an id.
  std::string select;
  // Initial camera (radians, radians, meters) when has_camera is set.
  float heading = 0, pitch = 0, distance = 0;
  bool has_camera = false;
  // Start in the top-down view.
  bool top = false;
  // Render a game preview at start (used for screenshots).
  bool preview = false;
  // Initial tool name: move, rotate, paint or place.
  std::string tool;
};

// Installs the editor's dark ImGui style, scaling all sizes by the display
// content scale.
void ApplyEditorTheme(float scale);

// The level editor: panels, viewport interaction and the edit commands over a
// Project. Frame() is called once per frame between ImGui::NewFrame and
// Render, on the thread that owns the GL context. All edits go through
// project_, and history_ snapshots them once no mouse or text input is
// active. The panels are in ui_panels.cpp, the rest in editor_app.cpp.
class EditorApp {
 public:
  // Loads the project, textures and models and opens the first level. Returns
  // false with a message in *error when that fails.
  bool Init(const EditorArgs& args, const Adapter* adapter, std::string* error);

  // Runs one frame: shortcuts, every panel, and the undo commit. `dt` is the
  // frame time in seconds.
  void Frame(float dt);

  // True once the user has confirmed quitting.
  bool WantsQuit() const { return quit_; }
  // True while a game preview is rendering.
  bool Busy() const { return preview_running_; }
  // True once a preview picture has been loaded.
  bool HasPreview() const { return preview_w_ > 0; }

  // Quits at once, or asks for confirmation when there are unsaved edits.
  void RequestQuit();

  // True when there are unsaved edits.
  bool Dirty() const { return project_.Dirty(); }

  // Title for the OS window (game name, level, and a star when dirty).
  std::string WindowTitle() const;

  // Rebuilds the default dock layout on the next frame.
  void ResetLayout() { layout_reset_ = true; }

 private:
  // Switches to a level, picking a valid context, and resets the view.
  void OpenLevel(const std::string& name);

  // Switches the context (a game-defined variant of the data, e.g. an era).
  void SetContext(const std::string& context);

  // The level and context being edited.
  Where Here() const { return {level_, context_}; }

  // Re-meshes the layers whose level changed and the model props, and applies
  // the cutaway and ghost styles. Called when the project revision moves.
  void RebuildScene();

  // Selects an entity.
  void Select(const EntityRef& r);

  void Deselect() { has_sel_ = false; }

  // Reads the pose of the selection; false when nothing valid is selected.
  bool SelectedPose(Pose* p) const;

  // World height of the floor of the level being edited (stacked levels sit
  // kFloorPitch apart).
  float FloorY() const;

  // World position of a pose on the current floor.
  Vec3 WorldPos(const Pose& p) const {
    return {static_cast<float>(p.x), FloorY() + static_cast<float>(p.y),
            static_cast<float>(p.z)};
  }

  // Model name of an entity, empty when it is not drawn as a model.
  std::string ModelOf(const EntityRef& r);

  bool VisualOf(const EntityRef& r, Visual* const v) const {
    return project_.Describe(Here(), r, v);
  }

  // Bounding box of a model's vertices (cached), or a small default box when
  // the model is missing.
  void ModelBounds(const std::string& model, Vec3* lo, Vec3* hi);

  // Oriented box (center, half extents, yaw for Viewport) used to draw and
  // pick an entity. Returns false when it has no pose or visual.
  bool EntityBox(const EntityRef& r, Vec3* centre, Vec3* half, float* yaw);

  // Nearest pickable entity under the ray, with its distance.
  bool PickEntity(const Ray& ray, EntityRef* out, float* dist);

  // Where the ray meets the level: the first solid wall cell it enters, else
  // the floor plane. Gives the hit point and its cell; false when off the map.
  bool RayLevel(const Ray& ray, Vec3* hit, int* cx, int* cz);

  // Rounds to the snap grid unless snapping is off or `fine` is held.
  float SnapValue(float v, bool fine) const;

  // Handles the camera, tool, shortcut and hover input for the viewport. The
  // mouse is relative to the viewport origin; `size` is its size in pixels.
  void ViewportInput(ImVec2 size, bool hovered, bool active);

  // Select / move / rotate: hover test of the gizmo handles, picking, and
  // dragging along an axis, in the floor plane, or about y.
  void GizmoInput(const Ray& ray, ImVec2 mouse, ImVec2 size);

  // Paint tool: stamps a brush along the drag (Bresenham line), a rectangle
  // with Shift, or picks the tile under the cursor with Ctrl.
  void PaintInput(const Ray& ray, bool pressed, bool down, bool released);

  // Place tool: adds furniture at the cell, or decor at the point hit (resting
  // on a prop when one is nearer).
  void PlaceInput(const Ray& ray, bool pressed);

  // Rebuilds the viewport's overlay shapes: grid, entity markers, boxes,
  // brush and placement previews, and the gizmo.
  void DrawOverlay(ImVec2 size);

  // ImGui draw-list overlay over the viewport: labels, the axis widget and
  // the status line.
  void DrawScreenOverlay(ImVec2 origin, ImVec2 size);

  // Moves the selection one step with the keys (a whole cell for furniture).
  void Nudge(float dx, float dz, float dy);

  void DeleteSelected();

  void DuplicateSelected();

  // Frames the selection, or the whole level when nothing is selected.
  void FocusSelection();

  // Centers the camera over the level.
  void FrameLevel();

  // Appends to the console (kept to 400 lines) and the SDL log.
  void Log(const std::string& line);

  // Writes every changed file; logs the outcome.
  void Save();

  // Steps back in the history, dropping a selection that no longer exists.
  void Undo();

  void Redo();

  // Sets the glyph of every cell of the rectangle (corners in any order,
  // clipped to the map).
  void PaintCells(int x0, int z0, int x1, int z1, char glyph);

  // Starts the game in its screenshot mode on a temporary copy of the assets
  // holding the unsaved edits. Runs asynchronously; see PollGamePreview.
  void RunGamePreview();

  // Collects a finished preview run and uploads its picture.
  void PollGamePreview();

  // Selects and frames what a problem refers to.
  void GoTo(const Problem& p);

  // Re-runs the project's checks for the current level.
  void RefreshProblems();

  // Looks for one of the adapter's game executables beside the editor.
  std::string FindGame() const;

  // The panel functions below each draw one dock window.
  void DrawDockSpace();

  void DrawMenuBar();

  void DrawLevelsPanel();

  void DrawOutliner();

  void DrawInspector();

  void DrawTriggers(LevelDoc& doc);

  void DrawRules(LevelDoc& doc);

  void DrawPrefabs(LevelDoc& doc);

  void DrawCutscenes();

  // The Prefab tool: click a cell to place the prefab chosen in the
  // Inspector's Prefabs section there (R turns it).
  // With Shift held it picks the area to capture instead: drag a rectangle
  // (it fills the Inspector's "From / To cell" fields).
  void PrefabInput(bool pressed, bool down, bool released);
  bool EnsurePrefab();

  void DrawPalette();

  void DrawAssets();

  void DrawConsole();

  void DrawProblems();

  void DrawViewportWindow(float dt);

  void DrawPreviewWindow();

  void DrawModals();

  // Draws editing widgets for a JSON value (colors, vectors and numbers get
  // dedicated widgets). Returns true when it changed; `depth` 0 is the root.
  bool EditJson(const char* label, mythos::json::Value& v, int depth);

  void DrawTileInfo(char glyph);

  void ToolButton(const char* label, Tool t, const char* tip);

  EditorArgs args_;
  Project project_;
  History history_;
  mythos::TextureLibrary textures_;
  mythos::ModelLibrary models_;
  std::vector<std::string> model_names_;
  // Cached per-model bounds, as (low, high) corners.
  std::map<std::string, std::pair<Vec3, Vec3>> bounds_;
  Viewport viewport_;
  OrbitCamera cam_;

  std::string level_, context_;
  EntityRef sel_;
  bool has_sel_ = false;
  std::string trigger_sel_;
  Trigger trigger_draft_;
  // Prefabs panel: the rectangle to save, the name, the cell and turns to
  // place.
  int prefab_rect_[4] = {0, 0, 1, 1};
  // The cutscene panel: the file being edited and its state.
  Cutscene cut_;
  std::string cut_file_, cut_new_name_ = "intro", cut_status_;
  bool cut_loaded_ = false;
  // The timeline: where the playhead is, whether the viewport shows the
  // cutscene's camera there, and the camera to go back to.
  float cut_scrub_ = 0.0f;
  bool cut_preview_ = false, cut_preview_was_ = false;
  OrbitCamera cut_saved_cam_;
  Prefab prefab_cache_;
  std::string prefab_cache_name_;
  std::string prefab_name_ = "group";
  std::string prefab_sel_;
  int prefab_at_[2] = {0, 0};
  int prefab_turns_ = 0;
  bool prefab_link_ = false;  // place as a link (updated when the file changes)
  std::string prefab_status_;
  std::string rule_sel_;
  EventRule rule_draft_;
  bool show_triggers_ = true;
  Tool tool_ = Tool::kSelect;
  char glyph_ = '#';
  int brush_ = 1;
  float snap_ = 0.25f;
  bool snap_rot_ = true;
  std::string place_model_ = "table";
  bool place_as_decor_ = false;
  float place_yaw_ = 0;

  bool cutaway_ = true, show_grid_ = true, show_ghosts_ = true,
       show_above_ = false, show_labels_ = false, show_lights_ = true,
       show_rooms_ = true, show_colliders_ = false;
  ShadeMode shade_ = ShadeMode::kTextured;

  // Project revision the scene was last built at, and per layer slot the
  // level that was meshed (so unchanged floors are not rebuilt).
  unsigned built_revision_ = 0;
  std::string slot_name_[Viewport::kSlots];
  LevelDoc slot_doc_[Viewport::kSlots];
  bool slot_valid_[Viewport::kSlots] = {};
  std::string built_level_, built_context_;

  // What a mouse drag in the viewport is doing.
  enum class Drag { kNone, kAxis, kPlane, kRotate, kPaint, kRect };

  // Camera drag: 0 orbit, 1 pan, 2 free look.
  bool cam_drag_ = false;
  int cam_mode_ = 0;
  Drag drag_ = Drag::kNone;
  Vec3 drag_axis_{}, drag_origin_{};
  float drag_t0_ = 0, drag_yaw0_ = 0, drag_angle0_ = 0;
  Pose drag_pose0_;
  Vec3 drag_offset_{};
  int rect_x0_ = 0, rect_z0_ = 0;
  bool hover_cell_valid_ = false;
  int hover_x_ = 0, hover_z_ = 0;
  Vec3 hover_point_{};
  bool hover_entity_valid_ = false;
  EntityRef hover_entity_;
  int hover_axis_ = -1;
  Rgba background_{0.11f, 0.12f, 0.14f, 1};
  ImVec2 view_origin_{0, 0}, view_size_{1, 1};
  bool view_hovered_ = false;
  unsigned view_texture_ = 0;
  float fps_ = 0;

  bool show_levels_ = true, show_outliner_ = true, show_inspector_ = true,
       show_palette_ = true, show_assets_ = true, show_console_ = true,
       show_problems_ = true, show_preview_ = true, show_demo_ = false,
       show_cutscene_ = false;
  bool layout_reset_ = false, layout_built_ = false;
  int focus_viewport_ = 0, focus_preview_ = 0;
  bool preview_started_ = false;
  std::deque<std::string> console_;
  std::vector<Problem> problems_;
  unsigned problems_revision_ = 0;
  std::string problems_level_;
  char filter_[96] = {};
  char model_filter_[96] = {};
  char level_filter_[96] = {};
  bool quit_ = false, confirm_quit_ = false, open_new_level_ = false,
       open_help_ = false, open_resize_ = false;
  int resize_w_ = 0, resize_h_ = 0;
  char new_name_[64] = "house_new";
  int new_w_ = 18, new_h_ = 13;

  // The game preview: a background job running the game, the picture it
  // writes and its GL texture.
  std::future<bool> preview_job_;
  bool preview_running_ = false;
  std::string preview_ppm_, preview_log_;
  unsigned preview_tex_ = 0;
  int preview_w_ = 0, preview_h_ = 0;
  std::map<std::string, int> preview_options_;
  std::string game_exe_;
};
}

#endif
