

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "engine/core/fileio.h"
#include "engine/editor/event_graph.h"
#include "engine/editor/prefab.h"
#include "engine/platform/editor/editor_app.h"
#include "imgui.h"
#include "imgui_internal.h"

// The editor's ImGui panels and menus, and the style they use.
namespace mythos::editor {
using mythos::json::Value;

namespace {
const ImU32 kAccent = IM_COL32(58, 130, 220, 255);

// InputText bound to a std::string (grows the string through a resize
// callback). Returns true when edited.
bool InputStr(const char* const label, std::string* const s,
              const ImGuiInputTextFlags flags = 0) {
  struct Cb {
    static int Resize(ImGuiInputTextCallbackData* const d) {
      if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(d->UserData);
        str->resize(static_cast<size_t>(d->BufTextLen));
        d->Buf = str->data();
      }
      return 0;
    }
  };
  s->reserve(s->size() + 1);
  return ImGui::InputText(label, s->data(), s->capacity() + 1,
                          flags | ImGuiInputTextFlags_CallbackResize,
                          Cb::Resize, s);
}

// Combo box over model names with a search field. Returns true when the
// choice changed.
bool ModelPicker(const char* const label, std::string* const model,
                 const std::vector<std::string>& names) {
  bool changed = false;
  if (ImGui::BeginCombo(label, model->empty() ? "(none)" : model->c_str(),
                        ImGuiComboFlags_HeightLarge)) {
    static char filter[64] = "";
    if (ImGui::IsWindowAppearing()) {
      filter[0] = 0;
      ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##f", "search", filter, sizeof filter);
    for (const auto& n : names) {
      if (filter[0] && n.find(filter) == std::string::npos) continue;
      if (ImGui::Selectable(n.c_str(), n == *model)) {
        *model = n;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

// One-line summary of a tile's properties for the palette.
std::string TileDescription(const mythos::TileInfo& t, const char g) {
  std::string s = std::string("'") + g + "'";
  if (g == '.') return s + "  plain floor";
  if (t.empty) s += "  open air";
  if (t.solid) s += "  solid";
  if (t.window)
    s += "  window";
  else if (t.thin)
    s += "  thin wall";
  if (t.door == mythos::DoorKind::kClosed) s += "  door (closed)";
  if (t.door == mythos::DoorKind::kOpen) s += "  door (open)";
  if (t.open_ceiling) s += "  stairwell up";
  if (t.open_floor) s += "  stair hole";
  if (!t.wall.empty()) s += "  wall: " + t.wall;
  if (!t.floor.empty()) s += "  floor: " + t.floor;
  if (!t.sprite.empty()) s += "  sprite: " + t.sprite;
  return s;
}
}

// Dark theme built from four background shades and one accent color.
void ApplyEditorTheme(const float scale) {
  ImGuiStyle& s = ImGui::GetStyle();
  ImGui::StyleColorsDark(&s);
  s.WindowRounding = 3;
  s.ChildRounding = 3;
  s.FrameRounding = 3;
  s.PopupRounding = 3;
  s.GrabRounding = 3;
  s.TabRounding = 3;
  s.ScrollbarRounding = 6;
  s.WindowBorderSize = 1;
  s.FrameBorderSize = 0;
  s.WindowPadding = {8, 8};
  s.FramePadding = {7, 4};
  s.ItemSpacing = {7, 5};
  s.TabBarBorderSize = 1;
  s.DockingSeparatorSize = 2;
  s.SeparatorTextBorderSize = 1;
  ImVec4* c = s.Colors;
  const ImVec4 bg0{0.075f, 0.080f, 0.090f, 1}, bg1{0.105f, 0.110f, 0.125f, 1},
      bg2{0.145f, 0.155f, 0.175f, 1}, bg3{0.200f, 0.215f, 0.240f, 1},
      accent{0.23f, 0.51f, 0.86f, 1}, accent_h{0.30f, 0.58f, 0.92f, 1};
  c[ImGuiCol_WindowBg] = bg1;
  c[ImGuiCol_ChildBg] = bg1;
  c[ImGuiCol_PopupBg] = {0.09f, 0.095f, 0.11f, 0.98f};
  c[ImGuiCol_Border] = {0.0f, 0.0f, 0.0f, 0.55f};
  c[ImGuiCol_FrameBg] = bg0;
  c[ImGuiCol_FrameBgHovered] = bg2;
  c[ImGuiCol_FrameBgActive] = bg3;
  c[ImGuiCol_TitleBg] = bg0;
  c[ImGuiCol_TitleBgActive] = bg0;
  c[ImGuiCol_MenuBarBg] = bg0;
  c[ImGuiCol_Header] = {accent.x, accent.y, accent.z, 0.38f};
  c[ImGuiCol_HeaderHovered] = {accent.x, accent.y, accent.z, 0.55f};
  c[ImGuiCol_HeaderActive] = {accent.x, accent.y, accent.z, 0.75f};
  c[ImGuiCol_Button] = bg3;
  c[ImGuiCol_ButtonHovered] = {0.27f, 0.29f, 0.33f, 1};
  c[ImGuiCol_ButtonActive] = accent;
  c[ImGuiCol_Tab] = bg0;
  c[ImGuiCol_TabHovered] = accent_h;
  c[ImGuiCol_TabSelected] = bg2;
  c[ImGuiCol_TabSelectedOverline] = accent;
  c[ImGuiCol_TabDimmed] = bg0;
  c[ImGuiCol_TabDimmedSelected] = bg1;
  c[ImGuiCol_TabDimmedSelectedOverline] = {0.3f, 0.3f, 0.35f, 1};
  c[ImGuiCol_DockingPreview] = {accent.x, accent.y, accent.z, 0.5f};
  c[ImGuiCol_DockingEmptyBg] = bg0;
  c[ImGuiCol_CheckMark] = accent_h;
  c[ImGuiCol_SliderGrab] = accent;
  c[ImGuiCol_SliderGrabActive] = accent_h;
  c[ImGuiCol_Separator] = {0, 0, 0, 0.5f};
  c[ImGuiCol_ResizeGrip] = {0, 0, 0, 0};
  c[ImGuiCol_TextSelectedBg] = {accent.x, accent.y, accent.z, 0.45f};
  c[ImGuiCol_Text] = {0.88f, 0.89f, 0.91f, 1};
  c[ImGuiCol_TextDisabled] = {0.50f, 0.52f, 0.56f, 1};
  s.ScaleAllSizes(scale);
}

// The frame rate shown is smoothed. Global shortcuts (save, undo, redo,
// duplicate, preview) are ignored while a text field is being edited. The
// undo snapshot is committed once no widget is active and no mouse button is
// down, so a whole drag or text edit becomes one undo step.
void EditorApp::Frame(const float dt) {
  ImGuiIO& io = ImGui::GetIO();
  fps_ =
      fps_ <= 0 ? 60.0f : fps_ * 0.95f + (dt > 0 ? 1.0f / dt : 60.0f) * 0.05f;
  if (args_.preview && !preview_started_ && ImGui::GetFrameCount() > 6) {
    preview_started_ = true;
    RunGamePreview();
  }
  const bool was_running = preview_running_;
  PollGamePreview();
  if (was_running && !preview_running_ && args_.preview) {
    focus_preview_ = 4;
  }

  if (!io.WantTextInput) {
    const bool ctrl = io.KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) Save();
    if (ctrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
    if (ctrl && ((io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) ||
                 ImGui::IsKeyPressed(ImGuiKey_Y)))
      Redo();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) DuplicateSelected();
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) RunGamePreview();
  }

  DrawDockSpace();
  DrawMenuBar();
  if (show_levels_) DrawLevelsPanel();
  if (show_outliner_) DrawOutliner();
  if (show_inspector_) DrawInspector();
  if (show_palette_) DrawPalette();
  if (show_assets_) DrawAssets();
  if (show_console_) DrawConsole();
  if (show_problems_) DrawProblems();
  if (show_cutscene_) DrawCutscenes();
  if (show_graph_) DrawEventGraph();
  DrawViewportWindow(dt);
  if (show_preview_) DrawPreviewWindow();
  if (show_demo_) ImGui::ShowDemoWindow(&show_demo_);
  DrawModals();

  if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(0) &&
      !ImGui::IsMouseDown(1) && !ImGui::IsMouseDown(2)) {
    if (history_.Commit(project_)) RefreshProblems();
  }
}

// Full-window dock host; the default layout is built the first time (or on
// ResetLayout) when ImGui has none saved.
void EditorApp::DrawDockSpace() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::SetNextWindowViewport(vp->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  ImGui::Begin("##dockhost", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking |
                   ImGuiWindowFlags_NoBackground);
  ImGui::PopStyleVar(3);
  const ImGuiID id = ImGui::GetID("EditorDockSpace");
  if (layout_reset_ ||
      (!layout_built_ && ImGui::DockBuilderGetNode(id) == nullptr)) {
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(id, vp->WorkSize);
    ImGuiID center = id, left, right, bottom, left_bottom, right_bottom;
    left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.19f, nullptr,
                                       &center);
    right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr,
                                        &center);
    bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.24f, nullptr,
                                         &center);
    left_bottom =
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.62f, nullptr, &left);
    right_bottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.40f,
                                               nullptr, &right);
    ImGui::DockBuilderDockWindow("Levels", left);
    ImGui::DockBuilderDockWindow("Outliner", left_bottom);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Tiles", right_bottom);
    ImGui::DockBuilderDockWindow("Assets", bottom);
    ImGui::DockBuilderDockWindow("Console", bottom);
    ImGui::DockBuilderDockWindow("Problems", bottom);
    ImGui::DockBuilderDockWindow("Game Preview", center);
    ImGui::DockBuilderDockWindow("Viewport", center);
    ImGui::DockBuilderFinish(id);
    layout_reset_ = false;
    focus_viewport_ = 4;
  }
  layout_built_ = true;
  ImGui::DockSpace(id, {0, 0}, ImGuiDockNodeFlags_PassthruCentralNode);
  ImGui::End();
}

// Main menu bar.
void EditorApp::DrawMenuBar() {
  if (!ImGui::BeginMainMenuBar()) return;
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New level...")) open_new_level_ = true;
    if (ImGui::MenuItem("Save all", "Ctrl+S", false, project_.Dirty())) Save();
    ImGui::Separator();
    if (ImGui::MenuItem("Quit")) RequestQuit();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit")) {
    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, history_.CanUndo())) Undo();
    if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, history_.CanRedo()))
      Redo();
    ImGui::Separator();
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, has_sel_))
      DuplicateSelected();
    if (ImGui::MenuItem("Delete", "Del", false, has_sel_)) DeleteSelected();
    if (ImGui::MenuItem("Deselect", "Esc", false, has_sel_)) Deselect();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    ImGui::MenuItem("Levels", nullptr, &show_levels_);
    ImGui::MenuItem("Outliner", nullptr, &show_outliner_);
    ImGui::MenuItem("Inspector", nullptr, &show_inspector_);
    ImGui::MenuItem("Tiles", nullptr, &show_palette_);
    ImGui::MenuItem("Assets", nullptr, &show_assets_);
    ImGui::MenuItem("Console", nullptr, &show_console_);
    ImGui::MenuItem("Problems", nullptr, &show_problems_);
    ImGui::MenuItem("Cutscene", nullptr, &show_cutscene_);
    ImGui::MenuItem("Event graph", nullptr, &show_graph_);
    ImGui::MenuItem("Game Preview", nullptr, &show_preview_);
    ImGui::Separator();
    if (ImGui::MenuItem("Frame level", "Home")) FrameLevel();
    if (ImGui::MenuItem("Frame selection", "F", false, has_sel_))
      FocusSelection();
    if (ImGui::MenuItem("Top view", "T")) cam_.TopDown();
    ImGui::Separator();
    if (ImGui::MenuItem("Reset layout")) ResetLayout();
    ImGui::MenuItem("ImGui demo", nullptr, &show_demo_);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Level")) {
    if (ImGui::MenuItem("Resize...")) open_resize_ = true;
    if (ImGui::MenuItem("Check for problems")) {
      RefreshProblems();
      show_problems_ = true;
      Log("Check: " + std::to_string(problems_.size()) + " problem(s).");
    }
    if (ImGui::MenuItem("Render in the game", "F5", false, !preview_running_))
      RunGamePreview();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Help")) {
    if (ImGui::MenuItem("Controls")) open_help_ = true;
    ImGui::EndMenu();
  }
  ImGui::SameLine(ImGui::GetWindowWidth() - 330);
  ImGui::TextDisabled("%s%s%s%s", level_.c_str(), project_.Dirty() ? " *" : "",
                      context_.empty() ? "" : "  |  ", context_.c_str());
  ImGui::EndMainMenuBar();
}

// Levels grouped by the adapter's grouping, with the context picker.
void EditorApp::DrawLevelsPanel() {
  if (!ImGui::Begin("Levels", &show_levels_)) {
    ImGui::End();
    return;
  }
  const auto contexts = project_.Contexts();
  const std::string cname = project_.adapter().ContextName();
  if (!contexts.empty()) {
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##context", (cname + " " + context_).c_str())) {
      for (const auto& e : contexts) {
        std::string label = cname;
        label += ' ';
        label += e;
        if (ImGui::Selectable(label.c_str(), e == context_)) SetContext(e);
      }
      ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The %s whose things and floors are shown",
                        cname.c_str());
  }
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##lf", "search levels", level_filter_,
                           sizeof level_filter_);
  std::map<std::pair<int, std::string>, std::vector<std::string>> groups;
  for (const auto& n : project_.LevelNames()) {
    if (level_filter_[0] && n.find(level_filter_) == std::string::npos)
      continue;
    const LevelInfo info = project_.Info(n);
    groups[{info.group_order, info.group.empty() ? "Levels" : info.group}]
        .push_back(n);
  }
  ImGui::BeginChild("##levels");
  for (auto& [key, names] : groups) {
    if (!ImGui::CollapsingHeader(key.second.c_str(),
                                 ImGuiTreeNodeFlags_DefaultOpen))
      continue;
    for (const auto& n : names) {
      const std::string label = n + (project_.IsDirty(n) ? "  *" : "");
      if (ImGui::Selectable(label.c_str(), n == level_)) OpenLevel(n);
      if (ImGui::IsItemHovered()) {
        const LevelInfo info = project_.Info(n);
        std::string used;
        for (const auto& e : info.contexts)
          used += (used.empty() ? "" : ", ") + e;
        const LevelDoc* d = project_.Level(n);
        if (info.contexts.empty() && contexts.empty())
          ImGui::SetTooltip("%dx%d cells", d->Width(), d->Height());
        else
          ImGui::SetTooltip("%dx%d cells\n%s: %s", d->Width(), d->Height(),
                            cname.c_str(),
                            used.empty() ? "(none)" : used.c_str());
      }
    }
  }
  ImGui::EndChild();
  ImGui::End();
}

// Entities of the level, grouped by kind and filtered by the search text.
void EditorApp::DrawOutliner() {
  if (!ImGui::Begin("Outliner", &show_outliner_)) {
    ImGui::End();
    return;
  }
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##of", "search", filter_, sizeof filter_);
  const auto all = project_.Entities(Here());
  struct Group {
    std::string name;
    std::vector<EntityRef> items;
  };
  std::vector<Group> groups;
  for (const auto& r : all) {
    const std::string name = project_.KindName(r.kind);
    auto g = std::ranges::find_if(
        groups, [&](const Group& x) { return x.name == name; });
    if (g == groups.end()) g = groups.insert(groups.end(), {name, {}});
    g->items.push_back(r);
  }
  ImGui::BeginChild("##outliner");
  for (const auto& g : groups) {
    if (g.items.empty()) continue;
    const std::string title =
        g.name + " (" + std::to_string(g.items.size()) + ")";
    if (!ImGui::CollapsingHeader(
            title.c_str(),
            g.items.size() < 60 ? ImGuiTreeNodeFlags_DefaultOpen : 0))
      continue;
    ImGuiListClipper clip;
    std::vector<const EntityRef*> shown;
    for (const auto& r : g.items) {
      if (filter_[0] &&
          project_.Label(Here(), r).find(filter_) == std::string::npos)
        continue;
      shown.push_back(&r);
    }
    clip.Begin(static_cast<int>(shown.size()));
    while (clip.Step())
      for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
        const EntityRef& r = *shown[static_cast<size_t>(i)];
        ImGui::PushID(i);
        if (ImGui::Selectable(project_.Label(Here(), r).c_str(),
                              has_sel_ && sel_ == r))
          Select(r);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
          FocusSelection();
        if (ImGui::BeginPopupContextItem()) {
          Select(r);
          if (ImGui::MenuItem("Frame", "F")) FocusSelection();
          if (ImGui::MenuItem("Duplicate", "Ctrl+D")) DuplicateSelected();
          if (ImGui::MenuItem("Delete", "Del")) DeleteSelected();
          ImGui::EndPopup();
        }
        ImGui::PopID();
      }
  }
  ImGui::EndChild();
  ImGui::End();
}

// Recursive editor of a JSON value: objects and arrays become trees, short
// numeric arrays a drag widget (a "color" triple a color picker), and
// scalars the matching widget. Keys starting with an underscore are hidden
// as comments. Returns true when anything changed.
bool EditorApp::EditJson(const char* const label, Value& v, const int depth) {
  bool changed = false;
  ImGui::PushID(label);
  if (v.is_object()) {
    if (depth == 0 ||
        ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen)) {
      for (auto it = v.begin(); it != v.end(); ++it) {
        if (it.key().starts_with('_')) continue;
        changed |= EditJson(it.key().c_str(), it.value(), depth + 1);
      }
      if (depth > 0) ImGui::TreePop();
    }
  } else if (v.is_array()) {
    const bool numeric =
        !v.empty() && v.size() <= 4 &&
        std::ranges::all_of(v, [](const Value& e) { return e.is_number(); });
    if (numeric && std::strcmp(label, "color") == 0 && v.size() == 3) {
      float c[3] = {v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
      if (ImGui::ColorEdit3(label, c, ImGuiColorEditFlags_Float)) {
        for (int i = 0; i < 3; ++i)
          v[static_cast<size_t>(i)] = std::round(c[i] * 1000.0) / 1000.0;
        changed = true;
      }
    } else if (numeric) {
      double d[4] = {};
      bool ints = true;
      for (size_t i = 0; i < v.size(); ++i) {
        d[i] = v[i].get<double>();
        ints &= v[i].is_number_integer();
      }
      if (ImGui::DragScalarN(label, ImGuiDataType_Double, d,
                             static_cast<int>(v.size()), ints ? 0.1f : 0.01f,
                             nullptr, nullptr, ints ? "%.0f" : "%.3f")) {
        for (size_t i = 0; i < v.size(); ++i) {
          if (ints)
            v[i] = static_cast<long long>(std::llround(d[i]));
          else
            v[i] = d[i];
        }
        changed = true;
      }
    } else if (ImGui::TreeNode(label, "%s [%zu]", label, v.size())) {
      for (size_t i = 0; i < v.size(); ++i) {
        const std::string n = std::to_string(i);
        changed |= EditJson(n.c_str(), v[i], depth + 1);
      }
      ImGui::TreePop();
    }
  } else if (v.is_boolean()) {
    bool b = v.get<bool>();
    if (ImGui::Checkbox(label, &b)) {
      v = b;
      changed = true;
    }
  } else if (v.is_number_integer()) {
    int i = v.get<int>();
    if (ImGui::DragInt(label, &i)) {
      v = i;
      changed = true;
    }
  } else if (v.is_number()) {
    double d = v.get<double>();
    if (ImGui::DragScalar(label, ImGuiDataType_Double, &d, 0.01f, nullptr,
                          nullptr, "%.3f")) {
      v = d;
      changed = true;
    }
  } else if (v.is_string()) {
    std::string s = v.get<std::string>();
    if (InputStr(label, &s)) {
      v = s;
      changed = true;
    }
  } else {
    ImGui::TextDisabled("%s: null", label);
  }
  ImGui::PopID();
  return changed;
}

// Describes a tile glyph.
void EditorApp::DrawTileInfo(const char glyph) {
  ImGui::TextWrapped("%s",
                     TileDescription(project_.Tiles()[glyph], glyph).c_str());
}

void EditorApp::DrawTriggers(LevelDoc& doc) {
  if (!ImGui::CollapsingHeader("Triggers", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  if (ImGui::SmallButton("Add trigger")) {
    Trigger t;
    t.id = NewTriggerId(doc);
    t.shape = TriggerShape::kBox;
    t.min = {0, FloorY(), 0};
    t.max = {1, FloorY() + 2.5, 1};
    t.floor = project_.Info(level_).floor;
    doc.triggers.push_back(t);
    project_.Touch(level_);
    trigger_sel_ = t.id;
    trigger_draft_ = t;
  }
  ImGui::SameLine();
  ImGui::Checkbox("Show volumes", &show_triggers_);

  if (ImGui::BeginCombo("Trigger", trigger_sel_.empty()
                                       ? "(select a trigger)"
                                       : trigger_sel_.c_str())) {
    for (size_t i = 0; i < doc.triggers.size(); ++i) {
      const Trigger& t = doc.triggers[i];
      ImGui::PushID(static_cast<int>(i));
      const bool clicked = ImGui::Selectable("##trigger", t.id == trigger_sel_);
      const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
      const ImVec4 clip{min.x, min.y, max.x, max.y};
      ImGui::GetWindowDrawList()->AddText(
          ImGui::GetFont(), ImGui::GetFontSize(),
          {min.x + ImGui::GetStyle().FramePadding.x,
           min.y + (max.y - min.y - ImGui::GetFontSize()) * 0.5f},
          ImGui::GetColorU32(ImGuiCol_Text), t.id.c_str(), nullptr, 0, &clip);
      if (clicked) {
        trigger_sel_ = t.id;
        trigger_draft_ = t;
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  auto selected = std::ranges::find_if(
      doc.triggers, [&](const Trigger& t) { return t.id == trigger_sel_; });
  if (selected == doc.triggers.end()) {
    trigger_sel_.clear();
    return;
  }

  InputStr("ID", &trigger_draft_.id);
  int shape = trigger_draft_.shape == TriggerShape::kBox ? 0 : 1;
  if (ImGui::Combo("Shape", &shape, "Box\0Cell\0")) {
    trigger_draft_.shape =
        shape == 0 ? TriggerShape::kBox : TriggerShape::kCell;
    if (trigger_draft_.shape == TriggerShape::kCell)
      trigger_draft_.floor = project_.Info(level_).floor;
  }
  int event = static_cast<int>(trigger_draft_.event);
  if (ImGui::Combo("Event", &event, "Enter\0Exit\0Stay\0Use\0"))
    trigger_draft_.event = static_cast<TriggerEvent>(event);
  ImGui::Checkbox("Once", &trigger_draft_.once);
  if (trigger_draft_.shape == TriggerShape::kCell) {
    int cell[2] = {trigger_draft_.cell_x, trigger_draft_.cell_z};
    if (ImGui::DragInt2("Cell X / Z", cell, 0.1f)) {
      trigger_draft_.cell_x = cell[0];
      trigger_draft_.cell_z = cell[1];
    }
    ImGui::InputInt("Floor", &trigger_draft_.floor);
    ImGui::TextDisabled("Cell volume has no height limit.");
  } else {
    double lo[3] = {trigger_draft_.min.x, trigger_draft_.min.y,
                    trigger_draft_.min.z};
    double hi[3] = {trigger_draft_.max.x, trigger_draft_.max.y,
                    trigger_draft_.max.z};
    if (ImGui::DragScalarN("Min XYZ (world)", ImGuiDataType_Double, lo, 3,
                           0.05f, nullptr, nullptr, "%.3f"))
      trigger_draft_.min = {lo[0], lo[1], lo[2]};
    if (ImGui::DragScalarN("Max XYZ (world)", ImGuiDataType_Double, hi, 3,
                           0.05f, nullptr, nullptr, "%.3f"))
      trigger_draft_.max = {hi[0], hi[1], hi[2]};
  }

  const bool valid_line = !SerializeTriggerLine(trigger_draft_).empty();
  const bool unique = std::ranges::none_of(doc.triggers, [&](const Trigger& t) {
    return t.id != trigger_sel_ && t.id == trigger_draft_.id;
  });
  const bool in_bounds =
      trigger_draft_.shape != TriggerShape::kCell ||
      doc.InBounds(trigger_draft_.cell_x, trigger_draft_.cell_z);
  if (!valid_line)
    ImGui::TextWrapped(
        "ID must be one token; box bounds must be finite and ordered.");
  else if (!unique)
    ImGui::TextWrapped("Trigger ID is already in use.");
  else if (!in_bounds)
    ImGui::TextWrapped("Cell must be inside the level map.");
  ImGui::TextWrapped(
      "Apply saves the draft to the level. Switching triggers discards it. "
      "Renaming a trigger updates the rules it drives; deleting it deletes "
      "them.");
  ImGui::BeginDisabled(!valid_line || !unique || !in_bounds ||
                       trigger_draft_ == *selected);
  if (ImGui::Button("Apply trigger")) {
    const std::string new_id = trigger_draft_.id;
    if (new_id != selected->id) RenameTrigger(doc, selected->id, new_id);
    *selected = trigger_draft_;
    trigger_sel_ = new_id;
    project_.Touch(level_);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Revert draft")) trigger_draft_ = *selected;
  ImGui::SameLine();
  if (ImGui::Button("Delete trigger")) {
    RemoveTrigger(doc, selected->id);
    trigger_sel_.clear();
    rule_sel_.clear();
    project_.Touch(level_);
  }
}

// Prefabs: groups of furniture and decor saved as
// `assets/prefabs/<name>.prefab` (the level file's own lines, see
// editor/prefab.h) and placed anywhere. A placed prefab is ordinary level
// items, so editing them afterwards overrides it.
void EditorApp::DrawPrefabs(LevelDoc& doc) {
  if (!ImGui::CollapsingHeader("Prefabs")) return;
  namespace fs = std::filesystem;
  const fs::path dir = fs::path(project_.AssetDir()) / "prefabs";

  ImGui::SeparatorText("Save an area");
  ImGui::DragInt2("From cell X / Y", &prefab_rect_[0], 0.2f);
  ImGui::DragInt2("To cell X / Y", &prefab_rect_[2], 0.2f);
  InputStr("Name", &prefab_name_);
  if (ImGui::Button("Save area as prefab")) {
    const Prefab p =
        CapturePrefab(doc, prefab_rect_[0], prefab_rect_[1], prefab_rect_[2],
                      prefab_rect_[3], SafePrefabName(prefab_name_));
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string path = (dir / (p.name + ".prefab")).string();
    if (p.furniture.empty() && p.decor.empty() && p.decals.empty() &&
        p.triggers.empty()) {
      prefab_status_ = "Nothing in that area.";
    } else if (!WriteFileAtomic(path, SerializePrefab(p))) {
      prefab_status_ = "Could not write " + path;
    } else {
      prefab_status_ = "Saved " + std::to_string(p.furniture.size()) +
                       " furniture, " + std::to_string(p.decor.size()) +
                       " decor, " + std::to_string(p.decals.size()) +
                       " decals, " + std::to_string(p.triggers.size()) +
                       " triggers and " + std::to_string(p.rules.size()) +
                       " rules to " + p.name + ".prefab";
      prefab_sel_ = p.name;
    }
  }

  ImGui::SeparatorText("Place");
  std::vector<std::string> names;
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end;
       it.increment(ec)) {
    if (it->path().extension() == ".prefab") {
      names.push_back(it->path().stem().string());
    }
  }
  std::ranges::sort(names);
  if (ImGui::BeginCombo("Prefab",
                        prefab_sel_.empty() ? "(none)" : prefab_sel_.c_str())) {
    for (const std::string& n : names) {
      if (ImGui::Selectable(n.c_str(), n == prefab_sel_)) prefab_sel_ = n;
    }
    ImGui::EndCombo();
  }
  ImGui::DragInt2("Top-left cell X / Y", prefab_at_, 0.2f);
  ImGui::Combo("Turn", &prefab_turns_,
               "0 degrees\0"
               "90 clockwise\0"
               "180\0"
               "90 counter-clockwise\0");
  ImGui::Checkbox("Link (update it when the prefab file changes)",
                  &prefab_link_);
  ImGui::BeginDisabled(prefab_sel_.empty());
  if (ImGui::Button("Place prefab")) {
    Prefab p;
    std::string error;
    const std::string path = (dir / (prefab_sel_ + ".prefab")).string();
    if (!ParsePrefab(ReadTextFile(path), &p, &error)) {
      prefab_status_ = "Cannot read " + prefab_sel_ + ": " + error;
    } else {
      const TileSet& tiles = project_.Tiles();
      p.name = prefab_sel_;
      const PlaceResult r = PlacePrefab(
          doc, p, prefab_at_[0], prefab_at_[1], prefab_turns_,
          [&](const char glyph) { return tiles.Solid(glyph); },
          prefab_link_ ? NewLinkId(doc) : std::string());
      if (r.furniture + r.decor + r.decals + r.triggers > 0)
        project_.Touch(level_);
      prefab_status_ = "Placed " + std::to_string(r.furniture) +
                       " furniture, " + std::to_string(r.decor) + " decor, " +
                       std::to_string(r.decals) + " decals, " +
                       std::to_string(r.triggers) + " triggers" +
                       (r.skipped > 0 ? ", skipped " + std::to_string(r.skipped)
                                      : std::string());
    }
  }
  ImGui::EndDisabled();

  // Linked placements: update from the file, unlink (keep the items) or remove.
  if (!doc.links.empty()) ImGui::SeparatorText("Links");
  for (size_t i = 0; i < doc.links.size(); ++i) {
    const PrefabLink link = doc.links[i];  // the buttons may erase it
    ImGui::PushID(static_cast<int>(i));
    ImGui::Text("%s: %s at %d, %d", link.id.c_str(), link.prefab.c_str(),
                link.x, link.y);
    ImGui::SameLine();
    const TileSet& tiles = project_.Tiles();
    if (ImGui::SmallButton("Update")) {
      Prefab p;
      std::string error;
      if (!ParsePrefab(ReadTextFile((dir / (link.prefab + ".prefab")).string()),
                       &p, &error)) {
        prefab_status_ = "Cannot read " + link.prefab + ": " + error;
      } else {
        p.name = link.prefab;
        PlaceResult r;
        UpdateLinked(
            doc, link.id, p,
            [&](const char glyph) { return tiles.Solid(glyph); }, &r);
        project_.Touch(level_);
        prefab_status_ = "Updated " + link.id + " (" +
                         std::to_string(r.furniture + r.decor) + " items)";
      }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Unlink")) {
      Unlink(doc, link.id);
      project_.Touch(level_);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove")) {
      RemoveLinked(doc, link.id);
      project_.Touch(level_);
      ImGui::PopID();
      break;  // the list changed
    }
    ImGui::PopID();
  }
  if (!prefab_status_.empty()) ImGui::TextWrapped("%s", prefab_status_.c_str());
}

// Event rules: when <trigger|flag|timer> and <flag conditions>, do <actions>.
// Edited as a draft and applied, like triggers, so a half-typed rule never
// reaches the level file.
void EditorApp::DrawRules(LevelDoc& doc) {
  if (!ImGui::CollapsingHeader("Rules")) return;
  if (ImGui::SmallButton("Add rule")) {
    EventRule r;
    r.id = NewRuleId(doc);
    r.event = {EventKind::kTrigger,
               doc.triggers.empty() ? "trigger_1" : doc.triggers[0].id, "enter",
               false};
    doc.rules.push_back(r);
    project_.Touch(level_);
    rule_sel_ = r.id;
    rule_draft_ = r;
  }
  if (ImGui::BeginCombo(
          "Rule", rule_sel_.empty() ? "(select a rule)" : rule_sel_.c_str())) {
    for (size_t i = 0; i < doc.rules.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::Selectable(doc.rules[i].id.c_str(),
                            doc.rules[i].id == rule_sel_)) {
        rule_sel_ = doc.rules[i].id;
        rule_draft_ = doc.rules[i];
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  const auto selected = std::ranges::find_if(
      doc.rules, [&](const EventRule& r) { return r.id == rule_sel_; });
  if (selected == doc.rules.end()) {
    rule_sel_.clear();
    return;
  }

  InputStr("Rule ID", &rule_draft_.id);
  int kind = static_cast<int>(rule_draft_.event.kind);
  if (ImGui::Combo("On", &kind, "Trigger\0Flag\0Timer\0"))
    rule_draft_.event.kind = static_cast<EventKind>(kind);
  InputStr("Event ID", &rule_draft_.event.id);
  if (rule_draft_.event.kind == EventKind::kTrigger) {
    InputStr("Detail (enter, exit, stay, use)", &rule_draft_.event.detail);
  } else if (rule_draft_.event.kind == EventKind::kFlag) {
    ImGui::Checkbox("New value", &rule_draft_.event.value);
  }
  ImGui::SeparatorText("Conditions");
  for (size_t i = 0; i < rule_draft_.conditions.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    InputStr("Flag", &rule_draft_.conditions[i].flag);
    ImGui::Checkbox("Is set", &rule_draft_.conditions[i].value);
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove")) {
      rule_draft_.conditions.erase(rule_draft_.conditions.begin() +
                                   static_cast<std::ptrdiff_t>(i));
      ImGui::PopID();
      break;
    }
    ImGui::PopID();
  }
  if (ImGui::SmallButton("Add condition"))
    rule_draft_.conditions.push_back({"flag", true});
  ImGui::SeparatorText("Actions");
  for (size_t i = 0; i < rule_draft_.actions.size(); ++i) {
    ImGui::PushID(1000 + static_cast<int>(i));
    InputStr("Type", &rule_draft_.actions[i].type);
    InputStr("Argument", &rule_draft_.actions[i].argument);
    if (ImGui::SmallButton("Remove")) {
      rule_draft_.actions.erase(rule_draft_.actions.begin() +
                                static_cast<std::ptrdiff_t>(i));
      ImGui::PopID();
      break;
    }
    ImGui::PopID();
  }
  if (ImGui::SmallButton("Add action"))
    rule_draft_.actions.push_back({"set_flag", ""});

  const bool valid = !SerializeEventRuleLine(rule_draft_).empty();
  const bool unique = std::ranges::none_of(doc.rules, [&](const EventRule& r) {
    return r.id != rule_sel_ && r.id == rule_draft_.id;
  });
  if (!valid)
    ImGui::TextWrapped(
        "Needs an ID, an event ID (and a detail for a trigger), and a type "
        "for every action and a flag for every condition.");
  else if (!unique)
    ImGui::TextWrapped("Rule ID is already in use.");
  ImGui::TextWrapped("Action types and arguments are the game's to run.");
  ImGui::BeginDisabled(!valid || !unique || rule_draft_ == *selected);
  if (ImGui::Button("Apply rule")) {
    *selected = rule_draft_;
    rule_sel_ = rule_draft_.id;
    project_.Touch(level_);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Revert rule")) rule_draft_ = *selected;
  ImGui::SameLine();
  if (ImGui::Button("Delete rule")) {
    doc.rules.erase(selected);
    rule_sel_.clear();
    project_.Touch(level_);
  }
}

// Level summary and the properties of the selection: furniture and decor
// are edited in place, anything else through its adapter data (EditJson).
void EditorApp::DrawInspector() {
  if (!ImGui::Begin("Inspector", &show_inspector_)) {
    ImGui::End();
    return;
  }
  LevelDoc* doc = project_.Level(level_);
  if (ImGui::CollapsingHeader("Level", ImGuiTreeNodeFlags_DefaultOpen) && doc) {
    const LevelInfo info = project_.Info(level_);
    ImGui::Text("%s", level_.c_str());
    ImGui::TextDisabled(
        "%s%s   %d x %d cells", info.group.c_str(),
        info.stacked ? (" - floor " + std::to_string(info.floor)).c_str() : "",
        doc->Width(), doc->Height());
    ImGui::TextDisabled("%zu furniture, %zu decor", doc->furniture.size(),
                        doc->decor.size());
    if (ImGui::SmallButton("Resize...")) open_resize_ = true;
  }

  if (doc) DrawTriggers(*doc);
  if (doc) DrawRules(*doc);
  if (doc) DrawPrefabs(*doc);

  Pose pose;
  if (has_sel_ && SelectedPose(&pose)) {
    ImGui::SeparatorText(project_.Label(Here(), sel_).c_str());
    bool level_changed = false, data_changed = false;
    if (doc && (sel_.kind == kFurniture || sel_.kind == kDecor)) {
      auto& list = sel_.kind == kFurniture ? doc->furniture : doc->decor;
      Item& it = list[static_cast<size_t>(sel_.index)];
      level_changed |= ModelPicker("Model", &it.model, model_names_);
      int cell[2] = {it.x, it.y};
      if (ImGui::DragInt2("Cell", cell, 0.1f)) {
        it.x = std::clamp(cell[0], 0, doc->Width() - 1);
        it.y = std::clamp(cell[1], 0, doc->Height() - 1);
        level_changed = true;
      }
      auto yaw = static_cast<float>(it.yaw);
      if (ImGui::SliderFloat("Yaw", &yaw, 0, 359.9f, "%.0f deg")) {
        it.yaw = snap_rot_ ? std::round(yaw / 15.0f) * 15.0f : yaw;
        level_changed = true;
      }
      {
        float off[2] = {static_cast<float>(it.off_x),
                        static_cast<float>(it.off_z)};
        if (ImGui::DragFloat2("Offset", off, 0.005f, -1.0f, 1.0f, "%.3f")) {
          it.off_x = off[0];
          it.off_z = off[1];
          level_changed = true;
        }
        auto lift = static_cast<float>(it.lift);
        if (ImGui::DragFloat("Lift", &lift, 0.005f, 0.0f, 3.0f, "%.3f m")) {
          it.lift = lift;
          level_changed = true;
        }
      }
      if (sel_.kind == kFurniture)
        ImGui::TextDisabled(
            "Furniture is solid in its cell; Offset and Lift place it freely.");
    } else {
      Value* d = project_.Data(Here(), sel_);
      float p[3] = {static_cast<float>(pose.x), static_cast<float>(pose.y),
                    static_cast<float>(pose.z)};
      Visual vis;
      VisualOf(sel_, &vis);
      bool moved = false;
      if (vis.edit_xz) {
        if (ImGui::DragFloat("X", &p[0], 0.01f, 0, 0, "%.3f")) moved = true;
        if (ImGui::DragFloat("Z", &p[2], 0.01f, 0, 0, "%.3f")) moved = true;
        if (!vis.y_label.empty() &&
            ImGui::DragFloat(vis.y_label.c_str(), &p[1], 0.01f, 0, 4, "%.3f"))
          moved = true;
        if (pose.has_yaw) {
          auto yaw = static_cast<float>(pose.yaw);
          if (ImGui::SliderFloat("Yaw", &yaw, -180, 360, "%.0f deg")) {
            pose.yaw = snap_rot_ ? std::round(yaw / 15.0f) * 15.0f : yaw;
            moved = true;
          }
        }
        if (moved) {
          pose.x = p[0];
          pose.y = p[1];
          pose.z = p[2];
          project_.SetPose(Here(), sel_, pose);
        }
      }
      if (d) {
        ImGui::SeparatorText("Data");
        data_changed |= EditJson("data", *d, 0);
      }
    }
    if (level_changed) project_.Touch(level_);
    if (data_changed) project_.TouchData();
  } else if (tool_ == Tool::kPaint) {
    ImGui::SeparatorText("Paint");
    DrawTileInfo(glyph_);
  } else if (tool_ == Tool::kPlace) {
    ImGui::SeparatorText("Place");
    ImGui::Text("%s", place_model_.c_str());
    ImGui::TextDisabled("Click in the view. R turns it.");
  } else {
    ImGui::Spacing();
    ImGui::TextDisabled(
        "Nothing selected. Click a thing in the view\nor in the Outliner.");
  }
  ImGui::End();
}

// Tile palette: brush size and one button per tile of the legend.
void EditorApp::DrawPalette() {
  if (!ImGui::Begin("Tiles", &show_palette_)) {
    ImGui::End();
    return;
  }
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Brush");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  ImGui::SliderInt("##brush", &brush_, 1, 9);
  ImGui::SameLine();
  ImGui::TextDisabled("[ and ] change it");
  const float cell = 44;
  const float avail = ImGui::GetContentRegionAvail().x;
  const int per_row = std::max(
      1, static_cast<int>(avail / (cell + ImGui::GetStyle().ItemSpacing.x)));
  int n = 0;
  ImGui::BeginChild("##tiles",
                    {0, -ImGui::GetTextLineHeightWithSpacing() * 2.4f});
  for (char g : project_.Glyphs()) {
    const mythos::TileInfo& t = project_.Tiles()[g];
    std::string tex = t.solid || t.door != mythos::DoorKind::kNone
                          ? project_.Tiles().WallTexture(g)
                          : project_.Tiles().FloorTexture(g);
    if (t.empty) tex = "missing";
    if (t.door != mythos::DoorKind::kNone && t.wall.empty()) tex = "door";
    ImGui::PushID(g);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool on = glyph_ == g;
    if (ImGui::ImageButton("##t",
                           static_cast<ImTextureID>(viewport_.TextureId(tex)),
                           {cell, cell}, {0, 1}, {1, 0})) {
      glyph_ = g;
      tool_ = Tool::kPaint;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled({p.x + 2, p.y + 2}, {p.x + 17, p.y + 17},
                      IM_COL32(0, 0, 0, 170), 2);
    const char s[2] = {g, 0};
    dl->AddText({p.x + 6, p.y + 2}, IM_COL32(255, 255, 255, 255), s);
    if (on)
      dl->AddRect(p,
                  {p.x + cell + 2 * ImGui::GetStyle().FramePadding.x,
                   p.y + cell + 2 * ImGui::GetStyle().FramePadding.y},
                  kAccent, 3.0f, 2.5f);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", TileDescription(t, g).c_str());
    ImGui::PopID();
    if (++n % per_row != 0) ImGui::SameLine();
  }
  ImGui::EndChild();
  ImGui::Separator();
  DrawTileInfo(glyph_);
  ImGui::End();
}

// Model and texture browsers.
void EditorApp::DrawAssets() {
  if (!ImGui::Begin("Assets", &show_assets_)) {
    ImGui::End();
    return;
  }
  if (ImGui::BeginTabBar("##assets")) {
    if (ImGui::BeginTabItem("Models")) {
      ImGui::SetNextItemWidth(220);
      ImGui::InputTextWithHint("##mf", "search models", model_filter_,
                               sizeof model_filter_);
      ImGui::SameLine();
      if (ImGui::RadioButton("Furniture", !place_as_decor_))
        place_as_decor_ = false;
      ImGui::SameLine();
      if (ImGui::RadioButton("Decor", place_as_decor_)) place_as_decor_ = true;
      ImGui::SameLine();
      ImGui::TextDisabled("Pick a model, then click in the view (N).");
      ImGui::BeginChild("##models");
      const int cols =
          std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / 170));
      if (ImGui::BeginTable("##modeltable", cols)) {
        for (const auto& m : model_names_) {
          if (model_filter_[0] && m.find(model_filter_) == std::string::npos)
            continue;
          ImGui::TableNextColumn();
          if (ImGui::Selectable(m.c_str(), place_model_ == m)) {
            place_model_ = m;
            tool_ = Tool::kPlace;
          }
          if (ImGui::IsItemHovered()) {
            Vec3 lo, hi;
            ModelBounds(m, &lo, &hi);
            ImGui::SetTooltip("%s\n%.2f x %.2f x %.2f m", m.c_str(),
                              hi.x - lo.x, hi.y - lo.y, hi.z - lo.z);
          }
        }
        ImGui::EndTable();
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Textures")) {
      ImGui::BeginChild("##textures");
      const float sz = 64;
      const int per_row = std::max(
          1, static_cast<int>(ImGui::GetContentRegionAvail().x / (sz + 10)));
      int n = 0;
      for (const auto& name : textures_.Names()) {
        ImGui::PushID(name.c_str());
        ImGui::Image(static_cast<ImTextureID>(viewport_.TextureId(name)),
                     {sz, sz}, {0, 1}, {1, 0});
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", name.c_str());
        ImGui::PopID();
        if (++n % per_row != 0) ImGui::SameLine();
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

// The cutscene editor: files in `assets/cutscenes`, their camera keys, cues,
// value tracks and animation keys as tables, a button that takes a camera key
// from the viewport, the problems, and Save. The format is world/cutscene.h's.
// The level's triggers, rules and flags as a graph: columns from what starts a
// chain to what it sets. Drag a node to move it (positions are kept for the
// level's `.graph` file), click a rule to edit it in the Rules section, and
// drag from the dot on the right of a node:
//   trigger, timer or flag -> rule: it becomes the event that starts the rule;
//   flag -> rule with Shift held: the rule reads the flag (Alt: stops reading);
//   rule -> flag: the rule sets that flag.
// "New rule" makes one from the selected trigger, timer or flag, and Delete
// removes the selected rule.
void EditorApp::ShowPanel(const std::string& name) {
  if (name == "graph") show_graph_ = true;
  if (name == "cutscene") show_cutscene_ = true;
  if (name == "inspector") show_inspector_ = true;
  if (name == "problems") show_problems_ = true;
}

std::string EditorApp::Summary() const {
  const LevelDoc* doc = project_.Level(level_);
  if (doc == nullptr) return "no level";
  size_t conditions = 0, actions = 0;
  for (const EventRule& r : doc->rules) {
    conditions += r.conditions.size();
    actions += r.actions.size();
  }
  std::string cam_times;
  for (const CameraKey& k : cut_.camera) {
    cam_times += (cam_times.empty() ? "" : ",") + FormatNumber(k.time);
  }
  return "level=" + level_ + " cam_times=" + cam_times +
         " conditions=" + std::to_string(conditions) +
         " actions=" + std::to_string(actions) +
         " rules=" + std::to_string(doc->rules.size()) +
         " triggers=" + std::to_string(doc->triggers.size()) +
         " links=" + std::to_string(doc->links.size()) +
         " furniture=" + std::to_string(doc->furniture.size()) +
         " decor=" + std::to_string(doc->decor.size()) +
         " decals=" + std::to_string(doc->decals.size()) +
         " cut_camera_keys=" + std::to_string(cut_.camera.size()) +
         " cut_cues=" + std::to_string(cut_.cues.size());
}

void EditorApp::DrawEventGraph() {
  ImGui::SetNextWindowPos({300, 60}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({780, 420}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Event graph", &show_graph_)) {
    ImGui::End();
    return;
  }
  LevelDoc* doc = project_.Level(level_);
  if (doc == nullptr) {
    ImGui::TextDisabled("No level open.");
    ImGui::End();
    return;
  }
  const std::string layout_path =
      project_.AssetDir() + "/levels/" + level_ + ".graph";
  if (graph_loaded_ != level_) {  // this level's saved layout
    graph_loaded_ = level_;
    graph_pos_.clear();
    graph_sel_.clear();
    for (const GraphPlacement& p :
         ParseGraphLayout(ReadTextFile(layout_path))) {
      graph_pos_[std::to_string(p.kind) + ":" + p.id] = {p.x, p.y};
    }
  }
  const auto save_layout = [&] {
    std::vector<GraphPlacement> placed;
    for (const auto& [key, at] : graph_pos_) {
      const size_t colon = key.find(':');
      placed.push_back({static_cast<int>(std::strtol(
                            key.substr(0, colon).c_str(), nullptr, 10)),
                        key.substr(colon + 1), at[0], at[1]});
    }
    WriteFileAtomic(layout_path, SerializeGraphLayout(placed));
  };
  const EventGraph graph = BuildEventGraph(*doc);
  if (ImGui::SmallButton("Reset layout")) {
    graph_pos_.clear();
    save_layout();
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("New trigger")) {
    // A one-metre box at the floor's origin, to move in the viewport.
    Trigger t;
    t.id = NewTriggerId(*doc);
    t.shape = TriggerShape::kBox;
    t.min = {0, FloorY(), 0};
    t.max = {1, FloorY() + 2.5, 1};
    t.floor = project_.Info(level_).floor;
    doc->triggers.push_back(t);
    project_.Touch(level_);
    trigger_sel_ = t.id;
    trigger_draft_ = t;
    graph_sel_ =
        std::to_string(static_cast<int>(GraphNodeKind::kTrigger)) + ":" + t.id;
  }
  ImGui::SameLine();
  {
    // The selected trigger, timer or flag starts a new rule.
    const bool have = !graph_sel_.empty();
    ImGui::BeginDisabled(!have);
    if (ImGui::SmallButton("New rule from selection")) {
      const size_t colon = graph_sel_.find(':');
      const auto kind = static_cast<GraphNodeKind>(static_cast<int>(
          std::strtol(graph_sel_.substr(0, colon).c_str(), nullptr, 10)));
      const std::string id =
          AddRuleFrom(*doc, kind, graph_sel_.substr(colon + 1));
      if (!id.empty()) {
        project_.Touch(level_);
        rule_sel_ = id;
        rule_draft_ = doc->rules.back();
      }
    }
    ImGui::EndDisabled();
  }
  ImGui::SameLine();
  ImGui::TextDisabled(
      "%zu triggers, %zu rules. Drag a dot to wire (Shift: condition, Alt: "
      "remove it); Delete removes the selected rule.",
      doc->triggers.size(), doc->rules.size());

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImVec2 avail = ImGui::GetContentRegionAvail();
  const bool editing_rule = std::ranges::any_of(
      doc->rules, [&](const EventRule& r) { return r.id == rule_sel_; });
  avail.y = std::max(avail.y - (editing_rule ? 150.0f : 0.0f), 200.0f);
  ImGui::InvisibleButton("##graph_canvas", avail,
                         ImGuiButtonFlags_MouseButtonLeft);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(origin, {origin.x + avail.x, origin.y + avail.y},
                    IM_COL32(24, 26, 32, 255));
  dl->PushClipRect(origin, {origin.x + avail.x, origin.y + avail.y}, true);

  constexpr float kW = 150.0f, kColumnGap = 70.0f, kRowGap = 14.0f,
                  kPad = 12.0f, kLine = 13.0f, kHead = 22.0f;
  const auto key_of = [](const GraphNode& n) {
    return std::to_string(static_cast<int>(n.kind)) + ":" + n.id;
  };
  const auto height_of = [&](const GraphNode& n) {
    return kHead + kLine * static_cast<float>(n.lines.size());
  };
  // The layout position of every node: column by column, rows stacked.
  std::vector<ImVec2> pos(graph.nodes.size());
  std::map<int, float> column_y;
  for (size_t i = 0; i < graph.nodes.size(); ++i) {
    const GraphNode& n = graph.nodes[i];
    float& y = column_y[n.column];
    pos[i] = {
        origin.x + kPad + static_cast<float>(n.column) * (kW + kColumnGap),
        origin.y + kPad + y};
    y += height_of(n) + kRowGap;
    if (const auto it = graph_pos_.find(key_of(n)); it != graph_pos_.end()) {
      pos[i] = {origin.x + it->second[0], origin.y + it->second[1]};
    }
  }
  const auto rect_min = [&](const size_t i) { return pos[i]; };
  const auto rect_max = [&](const size_t i) {
    return ImVec2{pos[i].x + kW, pos[i].y + height_of(graph.nodes[i])};
  };
  const auto out_pin = [&](const size_t i) {
    return ImVec2{pos[i].x + kW, pos[i].y + kHead / 2};
  };
  const auto in_pin = [&](const size_t i) {
    return ImVec2{pos[i].x, pos[i].y + kHead / 2};
  };

  // Wires first, under the nodes.
  for (const GraphEdge& e : graph.edges) {
    const ImU32 color =
        e.kind == GraphEdgeKind::kSets        ? IM_COL32(120, 220, 130, 220)
        : e.kind == GraphEdgeKind::kCondition ? IM_COL32(230, 120, 200, 220)
                                              : IM_COL32(255, 200, 80, 220);
    const ImVec2 a = out_pin(e.from), b = in_pin(e.to);
    const float bend = std::max(30.0f, std::fabs(b.x - a.x) * 0.4f);
    dl->AddBezierCubic(a, {a.x + bend, a.y}, {b.x - bend, b.y}, b, color, 1.6f);
    if (!e.label.empty()) {
      dl->AddText({(a.x + b.x) / 2 - 6, (a.y + b.y) / 2 - 14},
                  IM_COL32(200, 200, 210, 255), e.label.c_str());
    }
  }

  // Nodes: draggable, a rule selectable, and a dot to wire from.
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const bool mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  const bool mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  const bool over_canvas = ImGui::IsItemHovered();
  int wire_target = -1;
  for (size_t i = 0; i < graph.nodes.size(); ++i) {
    const GraphNode& n = graph.nodes[i];
    const ImVec2 lo = rect_min(i), hi = rect_max(i);
    const bool hovered = mouse.x >= lo.x && mouse.x <= hi.x &&
                         mouse.y >= lo.y && mouse.y <= hi.y;
    const bool selected = n.kind == GraphNodeKind::kRule && n.id == rule_sel_;
    const ImU32 fill =
        n.kind == GraphNodeKind::kTrigger ? IM_COL32(40, 70, 110, 255)
        : n.kind == GraphNodeKind::kTimer ? IM_COL32(90, 70, 40, 255)
        : n.kind == GraphNodeKind::kFlag  ? IM_COL32(70, 50, 80, 255)
                                          : IM_COL32(40, 80, 55, 255);
    dl->AddRectFilled(lo, hi, fill, 4.0f);
    dl->AddRect(lo, hi,
                selected  ? IM_COL32(255, 255, 255, 255)
                : hovered ? IM_COL32(180, 180, 200, 255)
                          : IM_COL32(90, 90, 110, 255),
                4.0f, selected ? 2.5f : 1.0f);
    const char* kind = n.kind == GraphNodeKind::kTrigger ? "trigger"
                       : n.kind == GraphNodeKind::kTimer ? "timer"
                       : n.kind == GraphNodeKind::kFlag  ? "flag"
                                                         : "rule";
    dl->AddText({lo.x + 6, lo.y + 4}, IM_COL32(235, 235, 240, 255),
                (std::string(kind) + " " + n.id).c_str());
    for (size_t l = 0; l < n.lines.size(); ++l) {
      dl->AddText({lo.x + 10, lo.y + kHead + kLine * static_cast<float>(l) - 2},
                  IM_COL32(190, 200, 190, 255), n.lines[l].c_str());
    }
    {
      const ImVec2 pin = out_pin(i);
      const bool pin_hot = (mouse.x - pin.x) * (mouse.x - pin.x) +
                               (mouse.y - pin.y) * (mouse.y - pin.y) <
                           64.0f;
      dl->AddCircleFilled(pin, 5.0f,
                          pin_hot || std::cmp_equal(graph_wire_, i)
                              ? IM_COL32(255, 255, 255, 255)
                              : IM_COL32(200, 200, 215, 255));
      if (over_canvas && mouse_clicked && pin_hot) {
        graph_wire_ = static_cast<int>(i);
        graph_drag_.clear();
        continue;
      }
    }
    if (graph_wire_ >= 0 && hovered && !std::cmp_equal(i, graph_wire_))
      wire_target = static_cast<int>(i);
    if (over_canvas && mouse_clicked && hovered && graph_wire_ < 0) {
      graph_drag_ = key_of(n);
      if (n.kind != GraphNodeKind::kRule) graph_sel_ = key_of(n);
      if (n.kind == GraphNodeKind::kRule) {
        graph_sel_.clear();
        const auto rule = std::ranges::find_if(
            doc->rules, [&](const EventRule& r) { return r.id == n.id; });
        if (rule != doc->rules.end()) {
          rule_sel_ = n.id;
          rule_draft_ = *rule;
        }
      }
    }
    if (graph_drag_ == key_of(n) && mouse_down) {
      const ImVec2 delta = ImGui::GetIO().MouseDelta;
      graph_pos_[graph_drag_] = {lo.x + delta.x - origin.x,
                                 lo.y + delta.y - origin.y};
    }
  }
  if (graph_wire_ >= 0) {
    const ImVec2 a = out_pin(static_cast<size_t>(graph_wire_));
    dl->AddLine(a, mouse, IM_COL32(255, 255, 255, 200), 2.0f);
    if (!mouse_down) {
      const GraphNode& source = graph.nodes[static_cast<size_t>(graph_wire_)];
      bool changed = false;
      std::string rule_to_show;
      if (wire_target >= 0) {
        const GraphNode& target = graph.nodes[static_cast<size_t>(wire_target)];
        const ImGuiIO& io = ImGui::GetIO();
        if (source.kind == GraphNodeKind::kRule &&
            target.kind == GraphNodeKind::kFlag) {
          changed = AddSetFlag(*doc, source.id, target.id);
          rule_to_show = source.id;
        } else if (source.kind != GraphNodeKind::kRule &&
                   target.kind == GraphNodeKind::kRule) {
          if (source.kind == GraphNodeKind::kFlag && io.KeyShift) {
            changed = AddCondition(*doc, target.id, source.id, true);
          } else if (source.kind == GraphNodeKind::kFlag && io.KeyAlt) {
            changed = RemoveCondition(*doc, target.id, source.id);
          } else {
            changed = Connect(*doc, source.kind, source.id, target.id);
          }
          rule_to_show = target.id;
        }
      }
      if (changed) {
        project_.Touch(level_);
        rule_sel_ = rule_to_show;
        const auto rule = std::ranges::find_if(
            doc->rules, [&](const EventRule& r) { return r.id == rule_sel_; });
        if (rule != doc->rules.end()) rule_draft_ = *rule;
      }
      graph_wire_ = -1;
    }
  }
  if (!mouse_down && !graph_drag_.empty()) {
    graph_drag_.clear();
    save_layout();  // a node was moved
  }
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      ImGui::IsKeyPressed(ImGuiKey_Delete) && !rule_sel_.empty() &&
      DeleteRule(*doc, rule_sel_)) {
    project_.Touch(level_);
    rule_sel_.clear();
  }
  dl->PopClipRect();

  // The selected rule's conditions and actions, edited in place.
  if (const auto rule = std::ranges::find_if(
          doc->rules, [&](const EventRule& r) { return r.id == rule_sel_; });
      rule != doc->rules.end()) {
    bool changed = false;
    ImGui::SeparatorText(("Rule " + rule->id).c_str());
    for (size_t i = 0; i < rule->conditions.size(); ++i) {
      EventCondition& c = rule->conditions[i];
      ImGui::PushID(static_cast<int>(i));
      ImGui::SetNextItemWidth(160);
      changed |= InputStr("##flag", &c.flag);
      ImGui::SameLine();
      changed |= ImGui::Checkbox("must be on", &c.value);
      ImGui::SameLine();
      if (ImGui::SmallButton("x")) {
        rule->conditions.erase(rule->conditions.begin() +
                               static_cast<std::ptrdiff_t>(i));
        changed = true;
        ImGui::PopID();
        break;
      }
      ImGui::PopID();
    }
    if (ImGui::SmallButton("Add condition")) {
      rule->conditions.push_back({"flag", true});
      changed = true;
    }
    for (size_t i = 0; i < rule->actions.size(); ++i) {
      EventAction& a = rule->actions[i];
      ImGui::PushID(1000 + static_cast<int>(i));
      ImGui::SetNextItemWidth(130);
      changed |= InputStr("##type", &a.type);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(300);
      changed |= InputStr("##arg", &a.argument);
      ImGui::SameLine();
      if (ImGui::SmallButton("x")) {
        rule->actions.erase(rule->actions.begin() +
                            static_cast<std::ptrdiff_t>(i));
        changed = true;
        ImGui::PopID();
        break;
      }
      ImGui::PopID();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Add action")) {
      rule->actions.push_back({"message", ""});
      changed = true;
    }
    if (changed) {
      project_.Touch(level_);
      rule_draft_ = *rule;
    }
  }
  ImGui::End();
}

void EditorApp::DrawCutscenes() {
  ImGui::SetNextWindowPos({300, 60}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({780, 560}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Cutscene", &show_cutscene_)) {
    ImGui::End();
    return;
  }
  namespace fs = std::filesystem;
  const fs::path dir = fs::path(project_.AssetDir()) / "cutscenes";
  std::error_code ec;
  std::vector<std::string> files;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end;
       it.increment(ec)) {
    if (it->path().extension() == ".cutscene") {
      files.push_back(it->path().stem().string());
    }
  }
  std::ranges::sort(files);

  if (ImGui::BeginCombo("File",
                        cut_file_.empty() ? "(none)" : cut_file_.c_str())) {
    for (const std::string& f : files) {
      if (ImGui::Selectable(f.c_str(), f == cut_file_)) {
        Cutscene loaded;
        std::string error;
        if (ParseCutscene(ReadTextFile((dir / (f + ".cutscene")).string()),
                          &loaded, &error)) {
          cut_ = std::move(loaded);
          cut_file_ = f;
          cut_loaded_ = true;
          cut_status_.clear();
        } else {
          cut_status_ = f;
          cut_status_ += ": ";
          cut_status_ += error;
        }
      }
    }
    ImGui::EndCombo();
  }
  InputStr("New name", &cut_new_name_);
  ImGui::SameLine();
  if (ImGui::SmallButton("New")) {
    cut_ = {};
    cut_.duration = 3.0;
    cut_file_ = SafePrefabName(cut_new_name_);
    cut_loaded_ = true;
    cut_status_ = "New cutscene: Save writes " + cut_file_ + ".cutscene";
  }
  if (!cut_loaded_) {
    if (!cut_status_.empty()) ImGui::TextWrapped("%s", cut_status_.c_str());
    ImGui::End();
    return;
  }

  const auto ease_combo = [](const char* const id, Ease* const ease) {
    ImGui::SetNextItemWidth(110);
    if (ImGui::BeginCombo(id, EaseName(*ease).c_str())) {
      for (int i = 0; i < kEaseCount; ++i) {
        const auto e = static_cast<Ease>(i);
        if (ImGui::Selectable(EaseName(e).c_str(), e == *ease)) *ease = e;
      }
      ImGui::EndCombo();
    }
  };
  const auto number = [](const char* const id, double* const v,
                         const float width, const float speed) {
    ImGui::SetNextItemWidth(width);
    ImGui::DragScalar(id, ImGuiDataType_Double, v, speed, nullptr, nullptr,
                      "%.3f");
  };

  ImGui::DragScalar("Duration (s)", ImGuiDataType_Double, &cut_.duration, 0.05f,
                    nullptr, nullptr, "%.2f");
  ImGui::Text("Length %.2f s", CutsceneLength(cut_));

  // The timeline: one row each of camera keys, cues, animation keys and value
  // keys over the length of the scene, and a playhead to drag. With the preview
  // on the viewport looks through the cutscene's camera at the playhead.
  ImGui::SeparatorText("Timeline");
  {
    const double length = std::max(0.1, CutsceneLength(cut_));
    cut_scrub_ = std::clamp(cut_scrub_, 0.0f, static_cast<float>(length));
    ImGui::SetNextItemWidth(200);
    ImGui::SliderFloat("Playhead (s)", &cut_scrub_, 0.0f,
                       static_cast<float>(length), "%.2f");
    ImGui::SameLine();
    ImGui::Checkbox("Camera preview", &cut_preview_);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::max(100.0f, ImGui::GetContentRegionAvail().x - 8);
    constexpr float kRow = 14.0f;
    const ImVec2 size{width, kRow * 4 + 4};
    ImGui::InvisibleButton("##timeline", size);
    // A press on a key's marker picks it and a drag moves it in time (snapped
    // to 0.05 s, Shift for free); a press anywhere else moves the playhead.
    const float mouse_t =
        std::clamp((ImGui::GetIO().MousePos.x - origin.x) / width, 0.0f, 1.0f);
    const double mouse_time = static_cast<double>(mouse_t) * length;
    if (ImGui::IsItemActivated()) {
      const int row = std::clamp(
          static_cast<int>((ImGui::GetIO().MousePos.y - origin.y - 2) / kRow),
          0, 3);
      cut_dragging_ = FindCutsceneKey(cut_, row, mouse_time,
                                      6.0 / static_cast<double>(width) * length,
                                      &cut_drag_);
    }
    if (ImGui::IsItemActive()) {
      if (cut_dragging_) {
        const double t = ImGui::GetIO().KeyShift
                             ? mouse_time
                             : std::round(mouse_time / 0.05) * 0.05;
        MoveCutsceneKey(cut_, cut_drag_, t);
        cut_scrub_ = static_cast<float>(t);
      } else {
        cut_scrub_ = static_cast<float>(mouse_time);
      }
    }
    if (ImGui::IsItemDeactivated()) cut_dragging_ = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                      IM_COL32(30, 30, 36, 255));
    const auto x_of = [&](const double t) {
      return origin.x + static_cast<float>(t / length) * width;
    };
    const auto mark = [&](const double t, const int row, const ImU32 color) {
      const float x = x_of(t),
                  y = origin.y + 2 + kRow * static_cast<float>(row) + kRow / 2;
      dl->AddCircleFilled({x, y}, 4.0f, color);
    };
    for (const CameraKey& k : cut_.camera)
      mark(k.time, 0, IM_COL32(90, 170, 255, 255));
    for (const CueKey& k : cut_.cues)
      mark(k.time, 1, IM_COL32(255, 200, 80, 255));
    for (const AnimKey& k : cut_.anims)
      mark(k.time, 2, IM_COL32(120, 220, 130, 255));
    for (const ValueTrack& t : cut_.values) {
      for (const ValueKey& k : t.keys)
        mark(k.time, 3, IM_COL32(230, 120, 200, 255));
    }
    const float px = x_of(cut_scrub_);
    dl->AddLine({px, origin.y}, {px, origin.y + size.y},
                IM_COL32(255, 255, 255, 255), 2.0f);
    ImGui::TextDisabled(
        "rows: camera, cues, animations, values; drag a key to retime it "
        "(Shift: no snap)");
    // Where the camera is at the playhead.
    CameraSample film;
    if (SampleCutsceneCamera(cut_, cut_scrub_, &film)) {
      ImGui::Text(
          "Camera at %.2f s: x %.2f  eye %.2f  z %.2f  yaw %.2f  pitch %.2f",
          cut_scrub_, film.pos.x, film.pos.y, film.pos.z, film.yaw, film.pitch);
      if (cut_preview_) {
        if (!cut_preview_was_) cut_saved_cam_ = cam_;
        const auto cp = static_cast<float>(std::cos(film.pitch));
        const float fx = cp * static_cast<float>(std::cos(film.yaw)),
                    fy = static_cast<float>(std::sin(film.pitch)),
                    fz = cp * static_cast<float>(std::sin(film.yaw));
        cam_.distance = 0.5f;
        cam_.heading = static_cast<float>(film.yaw);
        cam_.pitch = -static_cast<float>(film.pitch);
        cam_.target = {static_cast<float>(film.pos.x) + fx * 0.5f,
                       static_cast<float>(film.pos.y) + fy * 0.5f,
                       static_cast<float>(film.pos.z) + fz * 0.5f};
      }
    }
    if (!cut_preview_ && cut_preview_was_) cam_ = cut_saved_cam_;
    cut_preview_was_ = cut_preview_;
  }

  ImGui::SeparatorText("Camera keys");
  if (ImGui::SmallButton("Add key from the viewport camera")) {
    const OrbitCamera& oc = cam_;
    const Vec3 eye = oc.Eye();
    CameraKey k;
    k.time = cut_.camera.empty() ? 0.0 : cut_.camera.back().time + 1.0;
    k.pos = {eye.x, eye.y, eye.z};
    k.yaw = oc.heading;
    k.pitch = -oc.pitch;  // the editor looks down for positive; the game up
    cut_.camera.push_back(k);
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Add key")) cut_.camera.push_back({});
  for (size_t i = 0; i < cut_.camera.size(); ++i) {
    CameraKey& k = cut_.camera[i];
    ImGui::PushID(static_cast<int>(i));
    number("##t", &k.time, 60, 0.05f);
    ImGui::SameLine();
    number("##x", &k.pos.x, 60, 0.05f);
    ImGui::SameLine();
    number("##y", &k.pos.y, 60, 0.05f);
    ImGui::SameLine();
    number("##z", &k.pos.z, 60, 0.05f);
    ImGui::SameLine();
    number("##yaw", &k.yaw, 60, 0.01f);
    ImGui::SameLine();
    number("##pitch", &k.pitch, 60, 0.01f);
    ImGui::SameLine();
    number("##fov", &k.fov_scale, 50, 0.01f);
    ImGui::SameLine();
    ease_combo("##ease", &k.ease);
    ImGui::SameLine();
    const bool remove = ImGui::SmallButton("x");
    ImGui::PopID();
    if (remove) {
      cut_.camera.erase(cut_.camera.begin() + static_cast<std::ptrdiff_t>(i));
      break;
    }
  }
  if (!cut_.camera.empty()) {
    ImGui::TextDisabled("time, x, eye height, z, yaw, pitch, fov scale, ease");
  }

  ImGui::SeparatorText("Cues (the game carries them out)");
  if (ImGui::SmallButton("Add cue"))
    cut_.cues.push_back({0.0, {"message", ""}});
  for (size_t i = 0; i < cut_.cues.size(); ++i) {
    CueKey& k = cut_.cues[i];
    ImGui::PushID(1000 + static_cast<int>(i));
    number("##t", &k.time, 60, 0.05f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    InputStr("##type", &k.action.type);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    InputStr("##arg", &k.action.argument);
    ImGui::SameLine();
    const bool remove = ImGui::SmallButton("x");
    ImGui::PopID();
    if (remove) {
      cut_.cues.erase(cut_.cues.begin() + static_cast<std::ptrdiff_t>(i));
      break;
    }
  }

  ImGui::SeparatorText(
      "Value tracks (a fade, a position: the game reads them)");
  if (ImGui::SmallButton("Add track")) {
    cut_.values.push_back(
        {"value" + std::to_string(cut_.values.size() + 1), {}});
  }
  for (size_t i = 0; i < cut_.values.size(); ++i) {
    ValueTrack& t = cut_.values[i];
    ImGui::PushID(2000 + static_cast<int>(i));
    ImGui::SetNextItemWidth(140);
    InputStr("##name", &t.name);
    ImGui::SameLine();
    if (ImGui::SmallButton("Add key")) t.keys.push_back({});
    ImGui::SameLine();
    const bool remove_track = ImGui::SmallButton("Remove track");
    for (size_t j = 0; j < t.keys.size(); ++j) {
      ValueKey& k = t.keys[j];
      ImGui::PushID(static_cast<int>(j));
      ImGui::Indent();
      number("##t", &k.time, 60, 0.05f);
      ImGui::SameLine();
      number("##v", &k.value, 60, 0.01f);
      ImGui::SameLine();
      ease_combo("##ease", &k.ease);
      ImGui::SameLine();
      const bool remove = ImGui::SmallButton("x");
      ImGui::Unindent();
      ImGui::PopID();
      if (remove) {
        t.keys.erase(t.keys.begin() + static_cast<std::ptrdiff_t>(j));
        break;
      }
    }
    ImGui::PopID();
    if (remove_track) {
      cut_.values.erase(cut_.values.begin() + static_cast<std::ptrdiff_t>(i));
      break;
    }
  }

  ImGui::SeparatorText("Animation keys");
  if (ImGui::SmallButton("Add animation key")) {
    cut_.anims.push_back({0.0, "actor", "Idle", 0.2});
  }
  for (size_t i = 0; i < cut_.anims.size(); ++i) {
    AnimKey& k = cut_.anims[i];
    ImGui::PushID(3000 + static_cast<int>(i));
    number("##t", &k.time, 60, 0.05f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    InputStr("##actor", &k.actor);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    InputStr("##clip", &k.clip);
    ImGui::SameLine();
    number("##fade", &k.fade, 50, 0.01f);
    ImGui::SameLine();
    const bool remove = ImGui::SmallButton("x");
    ImGui::PopID();
    if (remove) {
      cut_.anims.erase(cut_.anims.begin() + static_cast<std::ptrdiff_t>(i));
      break;
    }
  }

  ImGui::Separator();
  const std::vector<std::string> problems = ValidateCutscene(cut_);
  for (const std::string& p : problems) {
    ImGui::TextColored({1.0f, 0.55f, 0.4f, 1.0f}, "%s", p.c_str());
  }
  ImGui::BeginDisabled(!problems.empty() || cut_file_.empty());
  if (ImGui::Button("Sort keys and save")) {
    SortCutscene(cut_);
    std::error_code make;
    fs::create_directories(dir, make);
    const std::string path = (dir / (cut_file_ + ".cutscene")).string();
    cut_status_ = WriteFileAtomic(path, SerializeCutscene(cut_))
                      ? "Saved " + cut_file_ + ".cutscene"
                      : "Could not write " + path;
  }
  ImGui::EndDisabled();
  if (!cut_status_.empty()) ImGui::TextWrapped("%s", cut_status_.c_str());
  ImGui::End();
}

// Log output; follows the end unless scrolled up.
void EditorApp::DrawConsole() {
  if (!ImGui::Begin("Console", &show_console_)) {
    ImGui::End();
    return;
  }
  if (ImGui::SmallButton("Clear")) console_.clear();
  ImGui::BeginChild("##console", {0, 0}, ImGuiChildFlags_Borders);
  for (const auto& l : console_) ImGui::TextUnformatted(l.c_str());
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4)
    ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
  ImGui::End();
}

// Results of the project's consistency checks; clicking one jumps to it.
void EditorApp::DrawProblems() {
  const std::string title =
      "Problems" +
      (problems_.empty() ? std::string()
                         : " (" + std::to_string(problems_.size()) + ")") +
      "###Problems";
  if (!ImGui::Begin(title.c_str(), &show_problems_)) {
    ImGui::End();
    return;
  }
  if (ImGui::SmallButton("Check now")) RefreshProblems();
  ImGui::SameLine();
  ImGui::TextDisabled(
      "Furniture in walls, things outside the map, missing models, stairwells "
      "without a hole.");
  ImGui::BeginChild("##problems", {0, 0}, ImGuiChildFlags_Borders);
  if (problems_.empty())
    ImGui::TextDisabled("No problems found in %s.", level_.c_str());
  for (size_t i = 0; i < problems_.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::Selectable(problems_[i].text.c_str())) GoTo(problems_[i]);
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::End();
}

// Toolbar button that stays highlighted while its tool is active.
void EditorApp::ToolButton(const char* const label, const Tool t,
                           const char* const tip) {
  const bool on = tool_ == t;
  if (on)
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
  if (ImGui::Button(label)) tool_ = t;
  if (on) ImGui::PopStyleColor();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
}

// Hosts the 3D view: input, scene rebuild, render into an offscreen texture
// shown as an image (flipped, as GL images are bottom-up), the overlays, and
// the floating toolbar (drawn in a second channel so its backdrop sits
// behind it).
void EditorApp::DrawViewportWindow(const float dt) {
  (void)dt;
  if (focus_viewport_ > 0) {
    ImGui::SetNextWindowFocus();
    --focus_viewport_;
  }
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  const bool open = ImGui::Begin(
      "Viewport", nullptr,
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  if (!open) {
    ImGui::End();
    return;
  }
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImVec2 size = ImGui::GetContentRegionAvail();
  size.x = std::max(size.x, 32.0f);
  size.y = std::max(size.y, 32.0f);
  view_origin_ = origin;
  view_size_ = size;

  ImGui::SetNextItemAllowOverlap();
  ImGui::InvisibleButton("##view", size,
                         ImGuiButtonFlags_MouseButtonLeft |
                             ImGuiButtonFlags_MouseButtonMiddle |
                             ImGuiButtonFlags_MouseButtonRight);
  const bool hovered = ImGui::IsItemHovered();
  view_hovered_ = hovered;
  if (hovered || ImGui::IsItemActive() || drag_ != Drag::kNone || cam_drag_)
    ViewportInput(size, hovered, ImGui::IsItemActive());
  else
    hover_cell_valid_ = hover_entity_valid_ = false;

  if (project_.Revision() != built_revision_) RebuildScene();
  DrawOverlay(size);
  const ImGuiIO& io = ImGui::GetIO();
  view_texture_ = viewport_.Render(
      cam_, static_cast<int>(size.x * io.DisplayFramebufferScale.x),
      static_cast<int>(size.y * io.DisplayFramebufferScale.y), shade_,
      background_);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddImage(static_cast<ImTextureID>(view_texture_), origin,
               {origin.x + size.x, origin.y + size.y}, {0, 1}, {1, 0});
  DrawScreenOverlay(origin, size);

  dl->ChannelsSplit(2);
  dl->ChannelsSetCurrent(1);
  ImGui::SetCursorScreenPos({origin.x + 10, origin.y + 10});
  ImGui::BeginGroup();
  ToolButton("Select", Tool::kSelect, "Select (Q)");
  ImGui::SameLine();
  ToolButton("Move", Tool::kMove, "Move (W)");
  ImGui::SameLine();
  ToolButton("Rotate", Tool::kRotate, "Rotate (E)");
  ImGui::SameLine();
  ToolButton("Paint", Tool::kPaint,
             "Paint tiles (B). Shift: rectangle. Ctrl+click: pick a tile.");
  ImGui::SameLine();
  ToolButton("Place", Tool::kPlace, "Place a model (N). R turns it.");
  ImGui::SameLine();
  ToolButton("Prefab", Tool::kPrefab,
             "Place the prefab chosen in the Inspector (P). R turns it. "
             "Shift+drag picks the area to save as a prefab.");
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(78);
  static const float snaps[] = {0, 1.0f, 0.5f, 0.25f, 0.1f, 0.05f};
  static const char* snap_names[] = {"Free",   "1 m",   "0.5 m",
                                     "0.25 m", "0.1 m", "0.05 m"};
  int si = 0;
  for (int i = 0; i < 6; ++i)
    if (std::abs(snaps[i] - snap_) < 1e-4f) si = i;
  if (ImGui::Combo("##snap", &si, snap_names, 6)) snap_ = snaps[si];
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Snap (hold Ctrl to move freely)");
  ImGui::SameLine();
  ImGui::Checkbox("15 deg", &snap_rot_);
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  if (ImGui::Button("View")) ImGui::OpenPopup("##viewopts");
  if (ImGui::BeginPopup("##viewopts")) {
    bool rebuild = false;
    rebuild |= ImGui::Checkbox("Cutaway (hide the ceiling)", &cutaway_);
    rebuild |= ImGui::Checkbox("Floors below", &show_ghosts_);
    rebuild |= ImGui::Checkbox("Floors above", &show_above_);
    ImGui::Checkbox("Grid", &show_grid_);
    ImGui::Checkbox("Labels", &show_labels_);
    ImGui::Checkbox("Lights", &show_lights_);
    ImGui::Checkbox("Dark rooms", &show_rooms_);
    ImGui::Checkbox("Collision boxes", &show_colliders_);
    ImGui::Checkbox("Trigger volumes", &show_triggers_);
    ImGui::Separator();
    int sm = static_cast<int>(shade_);
    ImGui::SetNextItemWidth(110);
    if (ImGui::Combo("Shading", &sm, "Textured\0Flat\0Wireframe\0"))
      shade_ = static_cast<ShadeMode>(sm);
    ImGui::SetNextItemWidth(110);
    ImGui::SliderFloat("Field of view", &cam_.fov_deg, 30, 100, "%.0f");
    if (rebuild) built_revision_ = 0;
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Frame")) FrameLevel();
  ImGui::SameLine();
  if (ImGui::Button("Top")) cam_.TopDown();
  ImGui::SameLine();
  ImGui::BeginDisabled(preview_running_);
  if (ImGui::Button(preview_running_ ? "Rendering..." : "Play (F5)"))
    RunGamePreview();
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "Render this view in the game, with your unsaved edits (Game Preview "
        "tab)");
  ImGui::EndGroup();
  const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  dl->ChannelsSetCurrent(0);
  dl->AddRectFilled({a.x - 6, a.y - 6}, {b.x + 6, b.y + 6},
                    IM_COL32(18, 19, 24, 205), 6);
  dl->ChannelsMerge();
  ImGui::End();
}

// Shows the picture the game rendered, with the adapter's preview options.
void EditorApp::DrawPreviewWindow() {
  if (focus_preview_ > 0) {
    ImGui::SetNextWindowFocus();
    --focus_preview_;
  }
  if (!ImGui::Begin("Game Preview", &show_preview_)) {
    ImGui::End();
    return;
  }
  const auto options = project_.adapter().PreviewOptions();
  if (project_.adapter().GameExecutables().empty()) {
    ImGui::TextDisabled("This editor has no game to preview in.");
    ImGui::End();
    return;
  }
  ImGui::BeginDisabled(preview_running_);
  if (ImGui::Button("Render in the game (F5)")) RunGamePreview();
  ImGui::EndDisabled();
  for (const PreviewOption& o : options) {
    ImGui::SameLine();
    int& v = preview_options_[o.key];
    if (o.toggle) {
      bool on = v != 0;
      if (ImGui::Checkbox(o.label.c_str(), &on)) v = on ? 1 : 0;
    } else {
      ImGui::SetNextItemWidth(70);
      ImGui::InputInt(o.label.c_str(), &v, 0, 0);
      v = std::max(v, o.min);
    }
  }
  ImGui::TextDisabled(
      "%s", preview_log_.empty()
                ? "Runs the game's screenshot mode from the viewport's camera "
                  "on a copy of the assets with your unsaved edits."
                : preview_log_.c_str());
  if (preview_tex_ && preview_w_ > 0) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float s = std::min(avail.x / preview_w_, avail.y / preview_h_);
    ImGui::Image(static_cast<ImTextureID>(preview_tex_),
                 {preview_w_ * s, preview_h_ * s});
  }
  ImGui::End();
}

// Modal dialogs: new level, resize, unsaved changes and the controls help.
// Other code only sets a flag; the popup is opened here, once per frame.
void EditorApp::DrawModals() {
  if (open_new_level_) {
    ImGui::OpenPopup("New level");
    open_new_level_ = false;
  }
  if (open_help_) {
    ImGui::OpenPopup("Controls");
    open_help_ = false;
  }
  if (open_resize_) {
    if (const LevelDoc* d = project_.Level(level_)) {
      resize_w_ = d->Width();
      resize_h_ = d->Height();
    }
    ImGui::OpenPopup("Resize level");
    open_resize_ = false;
  }
  if (confirm_quit_) {
    ImGui::OpenPopup("Unsaved changes");
    confirm_quit_ = false;
  }
  const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, {0.5f, 0.5f});
  if (ImGui::BeginPopupModal("New level", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::InputText("Name", new_name_, sizeof new_name_);
    ImGui::InputInt("Width", &new_w_);
    ImGui::InputInt("Height", &new_h_);
    ImGui::TextDisabled(
        "Letters, digits and _ . A level is a file in assets/levels.");
    if (ImGui::Button("Create", {100, 0})) {
      if (project_.CreateLevel(new_name_, new_w_, new_h_)) {
        OpenLevel(new_name_);
        ImGui::CloseCurrentPopup();
      } else {
        Log("Cannot create the level: the name is taken or not valid, or the "
            "size is out of range (3..200).");
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {100, 0})) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, {0.5f, 0.5f});
  if (ImGui::BeginPopupModal("Resize level", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::InputInt("Width", &resize_w_);
    ImGui::InputInt("Height", &resize_h_);
    ImGui::TextDisabled(
        "Rows and columns are added or removed at the right and bottom "
        "edges.\nFurniture, decor and cell triggers beyond the new edge are "
        "dropped.");
    if (ImGui::Button("Resize", {100, 0})) {
      if (LevelDoc* d = project_.Level(level_)) {
        if (resize_w_ >= 3 && resize_h_ >= 3 && resize_w_ <= 200 &&
            resize_h_ <= 200) {
          d->Resize(resize_w_, resize_h_, '.');
          project_.Touch(level_);
          Deselect();
          trigger_sel_.clear();
          FrameLevel();
        }
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {100, 0})) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, {0.5f, 0.5f});
  if (ImGui::BeginPopupModal("Unsaved changes", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("There are unsaved changes.");
    if (ImGui::Button("Save and quit", {130, 0})) {
      Save();
      quit_ = !project_.Dirty();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", {100, 0})) {
      quit_ = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {100, 0})) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, {0.5f, 0.5f});
  if (ImGui::BeginPopupModal("Controls", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::SeparatorText("Camera");
    ImGui::BulletText("Middle mouse (or Alt + left): orbit.  Shift: pan.");
    ImGui::BulletText(
        "Right mouse: look around, with W A S D to fly, Q E down and up, Shift "
        "faster.");
    ImGui::BulletText(
        "Wheel: zoom.  F: frame the selection.  Home: frame the level.  T: top "
        "view.");
    ImGui::SeparatorText("Tools");
    ImGui::BulletText(
        "Q select, W move, E rotate, B paint tiles, N place a model.");
    ImGui::BulletText(
        "Move: drag an arrow (one axis), the square (the floor), or the thing "
        "itself.");
    ImGui::BulletText(
        "Rotate: drag the ring (15 degree steps).  R turns by 90 degrees.");
    ImGui::BulletText(
        "Paint: drag.  Shift + drag: rectangle.  Ctrl + click: pick the tile.  "
        "[ ] brush size.");
    ImGui::BulletText(
        "Place: click in the view.  As decor it lands where you point, on a "
        "table if you point at one.");
    ImGui::BulletText(
        "Arrow keys nudge the selection, Page Up / Down lift it.  Hold Ctrl to "
        "ignore the snap.");
    ImGui::SeparatorText("Files");
    ImGui::BulletText(
        "Ctrl+S saves the levels and the data files you changed.  Ctrl+Z / "
        "Ctrl+Shift+Z undo and redo.");
    ImGui::BulletText(
        "Ctrl+D duplicates, Delete removes.  F5 renders the view in the game "
        "itself.");
    if (ImGui::Button("Close", {100, 0})) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}
}
