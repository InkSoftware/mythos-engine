

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

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
      if (sel_.kind == kDecor) {
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
        ImGui::TextDisabled("Furniture is solid and fills its cell.");
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
        "edges.\nFurniture and decor beyond the new edge are dropped.");
    if (ImGui::Button("Resize", {100, 0})) {
      if (LevelDoc* d = project_.Level(level_)) {
        if (resize_w_ >= 3 && resize_h_ >= 3 && resize_w_ <= 200 &&
            resize_h_ <= 200) {
          d->Resize(resize_w_, resize_h_, '.');
          project_.Touch(level_);
          Deselect();
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
