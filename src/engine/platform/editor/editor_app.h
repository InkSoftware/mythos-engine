#ifndef MYTHOS_ENGINE_PLATFORM_EDITOR_EDITOR_APP_H_
#define MYTHOS_ENGINE_PLATFORM_EDITOR_EDITOR_APP_H_
// The level editor: the project being edited, the 3D viewport and every panel
// around it (outliner, inspector, tile palette, asset browser, problems, game
// preview). run.cpp owns the window and the frame loop; it calls Frame().
// Whatever is particular to a game comes through its Adapter.

#include <deque>
#include <future>
#include <map>
#include <string>
#include <vector>

#include "engine/editor/adapter.h"
#include "engine/editor/history.h"
#include "engine/editor/project.h"
#include "engine/gfx/model.h"
#include "engine/gfx/textures.h"
#include "engine/platform/editor/viewport.h"
#include "imgui.h"

namespace mythos::editor {

enum class Tool { kSelect, kMove, kRotate, kPaint, kPlace };

struct EditorArgs {
  std::string project_dir;  // holds assets/ (the repository)
  std::string stage_dir;    // the merged assets (textures, models)
  std::string game_exe;     // the game, for the preview ("" = find it)
  std::string level;        // opened first ("" = the adapter's, or the first)
  std::string select;       // --select furniture:N|decor:N|<id> (screenshots)
  float heading = 0, pitch = 0,
        distance = 0;  // a camera given on the command line
  bool has_camera = false;
  bool top = false;
  bool preview = false;  // --preview: render in the game at start (screenshots)
  std::string tool;      // --tool select|move|rotate|paint|place
};

// The editor's look: a dark, flat theme in the manner of the big editors.
void ApplyEditorTheme(float scale);

class EditorApp {
 public:
  // The adapter (not owned) must outlive the app; null edits levels only.
  bool Init(const EditorArgs& args, const Adapter* adapter, std::string* error);
  // One frame: the dock space and every panel, then the viewport's picture.
  void Frame(float dt);
  bool WantsQuit() const { return quit_; }
  bool Busy() const { return preview_running_; }
  bool HasPreview() const { return preview_w_ > 0; }
  void RequestQuit();
  bool Dirty() const { return project_.Dirty(); }
  std::string WindowTitle() const;
  void ResetLayout() { layout_reset_ = true; }

 private:
  // ---- editor_app.cpp: state, scene, viewport interaction
  void OpenLevel(const std::string& name);
  void SetContext(const std::string& context);
  Where Here() const { return {level_, context_}; }
  void RebuildScene();
  void Select(const EntityRef& r);
  void Deselect() { has_sel_ = false; }
  bool SelectedPose(Pose* p) const;
  float FloorY() const;
  Vec3 WorldPos(const Pose& p) const {
    return {static_cast<float>(p.x), FloorY() + static_cast<float>(p.y),
            static_cast<float>(p.z)};
  }
  std::string ModelOf(const EntityRef& r);
  bool VisualOf(const EntityRef& r, Visual* v) const {
    return project_.Describe(Here(), r, v);
  }
  void ModelBounds(const std::string& model, Vec3* lo, Vec3* hi);
  bool EntityBox(const EntityRef& r, Vec3* centre, Vec3* half, float* yaw);
  bool PickEntity(const Ray& ray, EntityRef* out, float* dist);
  bool RayLevel(const Ray& ray, Vec3* hit, int* cx, int* cz);
  float SnapValue(float v, bool fine) const;

  void ViewportInput(ImVec2 size, bool hovered, bool active);
  void GizmoInput(const Ray& ray, ImVec2 mouse, ImVec2 size);
  void PaintInput(const Ray& ray, bool pressed, bool down, bool released);
  void PlaceInput(const Ray& ray, bool pressed);
  void DrawOverlay(ImVec2 size);
  void DrawScreenOverlay(ImVec2 origin, ImVec2 size);
  void Nudge(float dx, float dz, float dy);
  void DeleteSelected();
  void DuplicateSelected();
  void FocusSelection();
  void FrameLevel();
  void Log(const std::string& line);
  void Save();
  void Undo();
  void Redo();
  void PaintCells(int x0, int z0, int x1, int z1, char glyph);
  void RunGamePreview();
  void PollGamePreview();
  void GoTo(const Problem& p);
  void RefreshProblems();
  std::string FindGame() const;

  // ---- ui_panels.cpp
  void DrawDockSpace();
  void DrawMenuBar();
  void DrawLevelsPanel();
  void DrawOutliner();
  void DrawInspector();
  void DrawPalette();
  void DrawAssets();
  void DrawConsole();
  void DrawProblems();
  void DrawViewportWindow(float dt);
  void DrawPreviewWindow();
  void DrawModals();
  bool EditJson(const char* label, mythos::json::Value& v, int depth);
  void DrawTileInfo(char glyph);
  void ToolButton(const char* label, Tool t, const char* tip);

  EditorArgs args_;
  Project project_;
  History history_;
  mythos::TextureLibrary textures_;
  mythos::ModelLibrary models_;
  std::vector<std::string> model_names_;
  std::map<std::string, std::pair<Vec3, Vec3>> bounds_;
  Viewport viewport_;
  OrbitCamera cam_;

  std::string level_, context_;
  EntityRef sel_;
  bool has_sel_ = false;
  Tool tool_ = Tool::kSelect;
  char glyph_ = '#';
  int brush_ = 1;
  float snap_ = 0.25f;
  bool snap_rot_ = true;
  std::string place_model_ = "table";
  bool place_as_decor_ = false;
  float place_yaw_ = 0;

  // View options.
  bool cutaway_ = true, show_grid_ = true, show_ghosts_ = true,
       show_above_ = false, show_labels_ = false, show_lights_ = true,
       show_rooms_ = true, show_colliders_ = false;
  ShadeMode shade_ = ShadeMode::kTextured;

  // Scene cache.
  unsigned built_revision_ = 0;
  std::string slot_name_[Viewport::kSlots];
  LevelDoc slot_doc_[Viewport::kSlots];
  bool slot_valid_[Viewport::kSlots] = {};
  std::string built_level_, built_context_;

  // Viewport interaction.
  enum class Drag { kNone, kAxis, kPlane, kRotate, kPaint, kRect };
  bool cam_drag_ = false;
  int cam_mode_ = 0;  // 0 orbit, 1 pan, 2 look
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
  int hover_axis_ = -1;  // gizmo handle under the mouse: 0 x, 1 y, 2 z, 3 ring
  Rgba background_{0.11f, 0.12f, 0.14f, 1};
  ImVec2 view_origin_{0, 0}, view_size_{1, 1};
  bool view_hovered_ = false;
  unsigned view_texture_ = 0;
  float fps_ = 0;

  // Panels.
  bool show_levels_ = true, show_outliner_ = true, show_inspector_ = true,
       show_palette_ = true, show_assets_ = true, show_console_ = true,
       show_problems_ = true, show_preview_ = true, show_demo_ = false;
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

  // Game preview: the game run on a copy of the assets holding unsaved edits.
  std::future<bool> preview_job_;
  bool preview_running_ = false;
  std::string preview_ppm_, preview_log_;
  unsigned preview_tex_ = 0;
  int preview_w_ = 0, preview_h_ = 0;
  std::map<std::string, int> preview_options_;  // PreviewOption::key -> value
  std::string game_exe_;
};

}  // namespace mythos::editor

#endif  // MYTHOS_ENGINE_PLATFORM_EDITOR_EDITOR_APP_H_
