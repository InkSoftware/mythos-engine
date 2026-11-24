#include "engine/platform/editor/editor_app.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <sstream>

#include "engine/core/fileio.h"
#include "engine/gfx/mesh.h"
#include "engine/platform/gl/gl_api.h"

// Editor state, scene building, picking, tools and the game preview. The
// panels are drawn in ui_panels.cpp.
namespace mythos::editor {
namespace fs = std::filesystem;
using mythos::json::Value;

namespace {
// Whole-string number parse; `fallback` when the text is not a number.
int ParseInt(const std::string& text, const int fallback = 0) {
  char* end = nullptr;
  const int64_t v = std::strtoll(text.c_str(), &end, 10);
  return end != text.c_str() && *end == '\0' ? static_cast<int>(v) : fallback;
}

constexpr float kPi = std::numbers::pi_v<float>;
float Rad(const float deg) { return deg * kPi / 180.0f; }

// Game yaw (degrees, 0 faces +z, 90 faces +x) as the box yaw of RayHitsBox and
// WireBox, which turn the other way about y.
float BoxYaw(const float game_deg) { return -Rad(game_deg); }

const Rgba kYellow{1.0f, 0.82f, 0.2f, 1};
const Rgba kWhite{1, 1, 1, 0.9f};
const Rgba kRed{0.95f, 0.28f, 0.28f, 1};
const Rgba kGreen{0.4f, 0.85f, 0.35f, 1};
const Rgba kBlue{0.3f, 0.5f, 1.0f, 1};

// Distance in pixels from a point to a screen-space segment.
float DistToSegment(const ImVec2 p, const ImVec2 a, const ImVec2 b) {
  const float dx = b.x - a.x, dy = b.y - a.y;
  const float l2 = dx * dx + dy * dy;
  float t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0;
  t = std::clamp(t, 0.0f, 1.0f);
  const float cx = a.x + t * dx - p.x, cy = a.y + t * dy - p.y;
  return std::sqrt(cx * cx + cy * cy);
}

// Reads a binary P6 PPM (the game's screenshot format) into opaque RGBA.
// Returns false when the file is missing, malformed or truncated.
bool ReadPpm(const std::string& path, int* const w, int* const h,
             std::vector<uint8_t>* const rgba) {
  std::ifstream in(path, std::ios::binary);
  std::string magic;
  int maxv = 0;
  if (!(in >> magic >> *w >> *h >> maxv) || magic != "P6" || *w <= 0 || *h <= 0)
    return false;
  in.get();
  std::vector<uint8_t> rgb(static_cast<size_t>(*w) * *h * 3);
  in.read(reinterpret_cast<char*>(rgb.data()),
          static_cast<std::streamsize>(rgb.size()));
  if (in.gcount() != static_cast<std::streamsize>(rgb.size())) return false;
  rgba->resize(static_cast<size_t>(*w) * *h * 4);
  for (size_t i = 0; i < static_cast<size_t>(*w) * *h; ++i) {
    (*rgba)[i * 4] = rgb[i * 3];
    (*rgba)[i * 4 + 1] = rgb[i * 3 + 1];
    (*rgba)[i * 4 + 2] = rgb[i * 3 + 2];
    (*rgba)[i * 4 + 3] = 255;
  }
  return true;
}
}

// The first level is the one asked for, else the adapter's start level, else
// the first one in the project.
bool EditorApp::Init(const EditorArgs& args, const Adapter* const adapter,
                     std::string* const error) {
  args_ = args;
  project_ = Project(adapter);
  if (!project_.Load(args.project_dir, error)) return false;
  project_.adapter().SetupTextures(textures_, args.stage_dir);
  std::string merr;
  if (!models_.LoadDir(args.stage_dir + "/models", &merr)) {
    if (error) *error = "cannot load models: " + merr;
    return false;
  }
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(args.stage_dir + "/models", ec))
    if (e.path().extension() == ".obj")
      model_names_.push_back(e.path().stem().string());
  std::ranges::sort(model_names_);
  if (!viewport_.Init(&textures_)) {
    if (error) *error = "cannot start the viewport renderer";
    return false;
  }
  game_exe_ = args.game_exe.empty() ? FindGame() : args.game_exe;
  history_.Reset(project_);

  std::string first = args.level;
  if (first.empty()) first = project_.adapter().StartLevel(project_.data);
  if (!project_.Level(first)) first = project_.LevelNames().front();
  OpenLevel(first);
  if (args.has_camera) {
    cam_.heading = args.heading;
    cam_.pitch = args.pitch;
    if (args.distance > 0) cam_.distance = args.distance;
  }
  if (args.top) cam_.TopDown();
  if (args.tool == "move") tool_ = Tool::kMove;
  if (args.tool == "rotate") tool_ = Tool::kRotate;
  if (args.tool == "paint") tool_ = Tool::kPaint;
  if (args.tool == "place") tool_ = Tool::kPlace;
  if (!args.select.empty()) {
    const auto colon = args.select.find(':');
    const std::string kind = args.select.substr(0, colon),
                      what = args.select.substr(colon + 1);
    EntityRef r;
    if (kind == "furniture")
      r = {kFurniture, ParseInt(what), ""};
    else if (kind == "decor")
      r = {kDecor, ParseInt(what), ""};
    else
      for (const EntityRef& e : project_.Entities(Here()))
        if (e.id == what) r = e;
    Pose p;
    if (project_.GetPose(Here(), r, &p)) {
      Select(r);
      FocusSelection();
    }
  }
  for (const PreviewOption& o : project_.adapter().PreviewOptions())
    preview_options_[o.key] = o.value;
  Log("Opened " + args.project_dir + " - " +
      std::to_string(project_.LevelNames().size()) + " levels, " +
      std::to_string(model_names_.size()) + " models.");
  return true;
}

// Candidates are the adapter's executable paths relative to the editor's own
// folder.
std::string EditorApp::FindGame() const {
  std::vector<std::string> cands;
  if (const char* base = SDL_GetBasePath()) {
    for (const std::string& rel : project_.adapter().GameExecutables())
      cands.push_back((fs::path(base) / rel).lexically_normal().string());
  }
  for (const auto& c : cands) {
    std::error_code ec;
    if (fs::is_regular_file(c, ec)) return c;
  }
  return "";
}

void EditorApp::Log(const std::string& line) {
  SDL_Log("%s", line.c_str());
  console_.push_back(line);
  while (console_.size() > 400) console_.pop_front();
}

std::string EditorApp::WindowTitle() const {
  return project_.adapter().Name() + " - Level Editor - " + level_ +
         (project_.Dirty() ? " *" : "");
}

void EditorApp::RequestQuit() {
  if (project_.Dirty())
    confirm_quit_ = true;
  else
    quit_ = true;
}

// Keeps the current context when the new level supports it, else takes the
// level's first context.
void EditorApp::OpenLevel(const std::string& name) {
  if (!project_.Level(name)) return;
  level_ = name;
  const LevelInfo info = project_.Info(name);
  if (std::ranges::find(info.contexts, context_) == info.contexts.end()) {
    if (!info.contexts.empty())
      context_ = info.contexts.front();
    else if (context_.empty() && !project_.Contexts().empty())
      context_ = project_.Contexts().front();
  }
  Deselect();
  trigger_sel_.clear();
  rule_sel_.clear();
  RebuildScene();
  FrameLevel();
  RefreshProblems();
}

void EditorApp::SetContext(const std::string& context) {
  context_ = context;
  Deselect();
  trigger_sel_.clear();
  rule_sel_.clear();
  built_revision_ = 0;
  RefreshProblems();
}

// Levels of a stacked building are drawn at their floor's height.
float EditorApp::FloorY() const {
  const LevelInfo info = project_.Info(level_);
  return info.stacked ? static_cast<float>(info.floor) * mythos::kFloorPitch
                      : 0.0f;
}

// Looks at the level from the south at a raised angle, backed off to fit the
// larger side.
void EditorApp::FrameLevel() {
  const LevelDoc* doc = project_.Level(level_);
  if (!doc) return;
  cam_.target = {doc->Width() * 0.5f, FloorY(), doc->Height() * 0.5f};
  cam_.distance =
      std::clamp(std::max(doc->Width(), doc->Height()) * 1.15f, 6.0f, 120.0f);
  cam_.heading = -kPi / 2;
  cam_.pitch = 0.95f;
}

// Computed once per model from its vertices. A missing model gets a
// half-meter stand-in box so it can still be picked.
void EditorApp::ModelBounds(const std::string& model, Vec3* const lo,
                            Vec3* const hi) {
  auto it = bounds_.find(model);
  if (it == bounds_.end()) {
    Vec3 a{1e9f, 1e9f, 1e9f}, b{-1e9f, -1e9f, -1e9f};
    if (const mythos::Model* m = models_.Find(model)) {
      for (const auto& batch : m->batches)
        for (const auto& v : batch.verts) {
          a = {std::min(a.x, v.px), std::min(a.y, v.py), std::min(a.z, v.pz)};
          b = {std::max(b.x, v.px), std::max(b.y, v.py), std::max(b.z, v.pz)};
        }
    }
    if (a.x > b.x) {
      a = {-0.25f, 0, -0.25f};
      b = {0.25f, 0.5f, 0.25f};
    }
    it = bounds_.emplace(model, std::make_pair(a, b)).first;
  }
  *lo = it->second.first;
  *hi = it->second.second;
}

// Layer 0 is the level being edited; the other floors of its stack take the
// slots above (ghosts). A floor is re-meshed only when its document changed,
// the level or context switched, or its slot held something else. Model
// props of the game's entities are baked into the entity layer, rotated by
// their yaw. The cutaway clips everything above the ceiling line
// (2.46 m up, just under the ceiling at kWallHeight).
void EditorApp::RebuildScene() {
  const LevelInfo info = project_.Info(level_);
  const bool house = info.stacked;
  const bool outdoor = info.outdoor;
  std::vector<std::pair<int, std::string>> floors;
  if (house) floors = project_.Stack(context_, level_);
  if (floors.empty()) floors.emplace_back(info.floor, level_);

  const bool level_changed =
      built_level_ != level_ || built_context_ != context_;
  built_level_ = level_;
  built_context_ = context_;
  int slot = 0;
  bool active_seen = false;
  for (const auto& [floor, name] : floors) {
    const bool active = name == level_;
    if (active) active_seen = true;
    const int s = active ? 0 : ++slot;
    if (s >= Viewport::kEntitySlot) break;
    const LevelDoc* doc = project_.Level(name);
    if (!doc) {
      if (slot_valid_[s]) {
        viewport_.SetLayer(s, {});
        viewport_.SetDecals(s, {});
        slot_valid_[s] = false;
      }
      continue;
    }
    if (!slot_valid_[s] || slot_name_[s] != name || !(slot_doc_[s] == *doc) ||
        level_changed) {
      mythos::Level level;
      level.SetTiles(project_.TilesPtr());
      std::string err;
      level.ParseText(SerializeLevelDoc(*doc), &err);
      const float y =
          house ? static_cast<float>(floor) * mythos::kFloorPitch : 0.0f;
      mythos::SceneGeometry g =
          mythos::BuildSceneGeometry(level, models_, outdoor, y);
      viewport_.SetLayer(s, g.world);
      viewport_.SetDecals(s, g.decals);
      slot_name_[s] = name;
      slot_doc_[s] = *doc;
      slot_valid_[s] = true;
    }
    LayerStyle st;
    const float y =
        house ? static_cast<float>(floor) * mythos::kFloorPitch : 0.0f;
    st.clip_y = cutaway_ ? y + 2.46f : 1e9f;
    if (!active) {
      st.alpha = 0.32f;
      st.tint = 0.85f;
      st.visible = show_ghosts_ && (floor < info.floor || show_above_);
    }
    viewport_.SetStyle(s, st);
  }
  if (!active_seen && slot_valid_[0]) {
    viewport_.SetLayer(0, {});
    viewport_.SetDecals(0, {});
    slot_valid_[0] = false;
  }
  for (int s = slot + 1; s < Viewport::kEntitySlot; ++s) {
    if (slot_valid_[s]) {
      viewport_.SetLayer(s, {});
      viewport_.SetDecals(s, {});
    }
    slot_valid_[s] = false;
  }

  std::vector<mythos::Batch> props;
  for (const EntityRef& r : project_.Entities(Here())) {
    if (r.kind < kFirstGameKind) continue;
    Pose p;
    Visual vis;
    if (!VisualOf(r, &vis) || vis.shape != Shape::kModel) continue;
    const mythos::Model* m = models_.Find(vis.model);
    if (!m || !project_.GetPose(Here(), r, &p)) continue;
    const float c = std::cos(Rad(static_cast<float>(p.yaw))),
                sn = std::sin(Rad(static_cast<float>(p.yaw)));
    const float y = FloorY() + static_cast<float>(p.y);
    for (const auto& b : m->batches) {
      mythos::Batch nb;
      nb.texture = b.texture;
      nb.emissive = b.emissive;
      nb.alpha_test = b.alpha_test;
      nb.verts = b.verts;
      for (auto& v : nb.verts) {
        const float x = v.px * c + v.pz * sn, z = -v.px * sn + v.pz * c;
        v.px = x + static_cast<float>(p.x);
        v.pz = z + static_cast<float>(p.z);
        v.py += y;
        const float nx = v.nx * c + v.nz * sn, nz = -v.nx * sn + v.nz * c;
        v.nx = nx;
        v.nz = nz;
      }
      props.push_back(std::move(nb));
    }
  }
  viewport_.SetLayer(Viewport::kEntitySlot, props);
  LayerStyle es;
  es.clip_y = cutaway_ ? FloorY() + 2.46f : 1e9f;
  viewport_.SetStyle(Viewport::kEntitySlot, es);
  built_revision_ = project_.Revision();
}

void EditorApp::Select(const EntityRef& r) {
  sel_ = r;
  has_sel_ = true;
}

bool EditorApp::SelectedPose(Pose* const p) const {
  return has_sel_ && project_.GetPose(Here(), sel_, p);
}

std::string EditorApp::ModelOf(const EntityRef& r) {
  Visual v;
  return VisualOf(r, &v) && v.shape == Shape::kModel ? v.model : "";
}

// Models use their bounds turned about the pose; lights, markers and actors
// use fixed-size boxes; a region spans its rectangle at floor height.
bool EditorApp::EntityBox(const EntityRef& r, Vec3* const centre,
                          Vec3* const half, float* const yaw) {
  Pose p;
  Visual v;
  if (!project_.GetPose(Here(), r, &p) || !VisualOf(r, &v)) return false;
  const Vec3 pos = WorldPos(p);
  switch (v.shape) {
    case Shape::kModel: {
      Vec3 lo, hi;
      ModelBounds(v.model, &lo, &hi);
      const Vec3 c = (lo + hi) * 0.5f;
      const float c_y = Rad(static_cast<float>(p.yaw));
      const float cs = std::cos(c_y), sn = std::sin(c_y);
      *centre = pos + Vec3{c.x * cs + c.z * sn, c.y, -c.x * sn + c.z * cs};
      *half = (hi - lo) * 0.5f + Vec3{0.01f, 0.01f, 0.01f};
      *yaw = BoxYaw(static_cast<float>(p.yaw));
      return true;
    }
    case Shape::kLight:
      *centre = pos;
      *half = {0.16f, 0.16f, 0.16f};
      *yaw = 0;
      return true;
    case Shape::kMarker:
      *centre = pos;
      *half = {0.18f, 0.18f, 0.18f};
      *yaw = 0;
      return true;
    case Shape::kActor:
      *centre = pos + Vec3{0, 0.85f, 0};
      *half = {0.25f, 0.85f, 0.25f};
      *yaw = 0;
      return true;
    case Shape::kRegion: {
      const float fy = FloorY();
      *centre = {(v.x0 + v.x1) / 2, fy + 1.25f, (v.z0 + v.z1) / 2};
      *half = {(v.x1 - v.x0) / 2, 1.25f, (v.z1 - v.z0) / 2};
      *yaw = 0;
      return true;
    }
  }
  return false;
}

// Skips entities that are not pickable, hidden lights, and (with the cutaway)
// those entirely above the cut.
bool EditorApp::PickEntity(const Ray& ray, EntityRef* const out,
                           float* const dist) {
  float best = 1e30f;
  bool found = false;
  for (const EntityRef& r : project_.Entities(Here())) {
    Visual vis;
    if (!VisualOf(r, &vis) || !vis.pickable) continue;
    if (vis.shape == Shape::kLight && !show_lights_) continue;
    Vec3 c, h;
    float yaw;
    if (!EntityBox(r, &c, &h, &yaw)) continue;
    if (cutaway_ && c.y - h.y > FloorY() + 2.46f) continue;
    const float t = RayHitsBox(ray, c, h, yaw);
    if (t >= 0 && t < best) {
      best = t;
      *out = r;
      found = true;
    }
  }
  if (found && dist) *dist = best;
  return found;
}

// Marches the ray in 5 cm steps up to the floor plane (or 250 m), looking
// for a solid cell between the floor and the cutaway height, so clicking a
// wall picks the wall's cell rather than the floor behind it.
bool EditorApp::RayLevel(const Ray& ray, Vec3* const hit, int* const cx,
                         int* const cz) {
  const LevelDoc* doc = project_.Level(level_);
  if (!doc) return false;
  const float fy = FloorY();
  const float t_ground = RayHitsHorizontalPlane(ray, fy);
  const float t_end = t_ground >= 0 ? t_ground : 250.0f;
  const int steps = static_cast<int>(std::ceil(t_end / 0.05f));
  for (int step = 0; step < steps; ++step) {
    const Vec3 p = ray.At(static_cast<float>(step) * 0.05f);
    const int x = static_cast<int>(std::floor(p.x)),
              z = static_cast<int>(std::floor(p.z));
    if (doc->InBounds(x, z) && p.y > fy && p.y < fy + 2.46f &&
        project_.Tiles().Solid(doc->At(x, z))) {
      *hit = p;
      *cx = x;
      *cz = z;
      return true;
    }
  }
  if (t_ground < 0) return false;
  *hit = ray.At(t_ground);
  *cx = static_cast<int>(std::floor(hit->x));
  *cz = static_cast<int>(std::floor(hit->z));
  return doc->InBounds(*cx, *cz);
}

float EditorApp::SnapValue(const float v, const bool fine) const {
  if (snap_ <= 0 || fine) return v;
  return std::round(v / snap_) * snap_;
}

// Everything moves by the snap step (or 5 cm when snapping is off); furniture
// too, though it stays solid in the cell its centre is in.
void EditorApp::Nudge(const float dx, const float dz, const float dy) {
  Pose p;
  if (!SelectedPose(&p)) return;
  const float step = snap_ > 0 ? snap_ : 0.05f;
  p.x += dx * step;
  p.z += dz * step;
  p.y += dy * (snap_ > 0 ? snap_ : 0.05f);
  project_.SetPose(Here(), sel_, p);
}

// Some entities cannot be removed (the project refuses); say why.
void EditorApp::DeleteSelected() {
  if (!has_sel_) return;
  if (!project_.Remove(Here(), sel_)) {
    Log("A prop cannot be deleted here: it carries the story's text. Edit or "
        "hide it in the data files.");
    return;
  }
  Deselect();
}

void EditorApp::DuplicateSelected() {
  if (!has_sel_) return;
  const EntityRef r = project_.Duplicate(Here(), sel_);
  if (r.index >= 0)
    Select(r);
  else
    Log("That kind of thing cannot be duplicated.");
}

void EditorApp::FocusSelection() {
  Vec3 c, h;
  float yaw;
  if (has_sel_ && EntityBox(sel_, &c, &h, &yaw))
    cam_.FocusOn(c, std::max({h.x, h.y, h.z}));
  else
    FrameLevel();
}

void EditorApp::Save() {
  std::string err;
  if (project_.Save(&err))
    Log("Saved.");
  else
    Log("Save failed: " + err);
}

// The edits are restored from a snapshot, so the selection may point at
// something gone; checks must be redone.
void EditorApp::Undo() {
  if (history_.Undo(&project_)) {
    trigger_sel_.clear();
    rule_sel_.clear();
    if (has_sel_) {
      Pose p;
      if (!project_.GetPose(Here(), sel_, &p)) Deselect();
    }
    problems_revision_ = 0;
  }
}

void EditorApp::Redo() {
  if (history_.Redo(&project_)) {
    trigger_sel_.clear();
    rule_sel_.clear();
    if (has_sel_) {
      Pose p;
      if (!project_.GetPose(Here(), sel_, &p)) Deselect();
    }
    problems_revision_ = 0;
  }
}

void EditorApp::RefreshProblems() {
  problems_ = project_.Check(Here(), model_names_);
  problems_revision_ = project_.Revision();
  problems_level_ = level_;
}

void EditorApp::GoTo(const Problem& p) {
  if (p.has_ref) {
    Select(p.ref);
    FocusSelection();
  } else {
    Deselect();
    cam_.target = {static_cast<float>(p.x), FloorY(), static_cast<float>(p.z)};
    cam_.distance = 8;
  }
}

// Only touches the project when a cell actually changes, so repainting a
// glyph adds no undo step.
void EditorApp::PaintCells(const int x0, const int z0, const int x1,
                           const int z1, const char glyph) {
  LevelDoc* doc = project_.Level(level_);
  if (!doc) return;
  bool changed = false;
  for (int z = std::min(z0, z1); z <= std::max(z0, z1); ++z)
    for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x)
      if (doc->InBounds(x, z) && doc->At(x, z) != glyph) {
        doc->Set(x, z, glyph);
        changed = true;
      }
  if (changed) project_.Touch(level_);
}

// A drag draws a Bresenham line from the last cell to the hovered one, so a
// fast stroke leaves no gaps (guarded to 400 steps). Shift-drag defers the
// fill to the release.
void EditorApp::PaintInput(const Ray& ray, const bool pressed, const bool down,
                           const bool released) {
  (void)ray;
  const ImGuiIO& io = ImGui::GetIO();
  const LevelDoc* doc = project_.Level(level_);
  if (!doc || !hover_cell_valid_) {
    if (released) drag_ = Drag::kNone;
    return;
  }
  const int b = std::max(1, brush_);
  auto stamp = [&](const int cx, const int cz) {
    PaintCells(cx - (b - 1) / 2, cz - (b - 1) / 2, cx + b / 2, cz + b / 2,
               glyph_);
  };
  if (pressed) {
    if (io.KeyCtrl) {
      glyph_ = doc->At(hover_x_, hover_z_);
      Log(std::string("Picked tile '") + glyph_ + "'.");
      return;
    }
    rect_x0_ = hover_x_;
    rect_z0_ = hover_z_;
    if (io.KeyShift) {
      drag_ = Drag::kRect;
    } else {
      drag_ = Drag::kPaint;
      stamp(hover_x_, hover_z_);
    }
  } else if (down && drag_ == Drag::kPaint) {
    int x = rect_x0_, z = rect_z0_;
    const int dx = std::abs(hover_x_ - x), dz = std::abs(hover_z_ - z);
    const int sx = x < hover_x_ ? 1 : -1, sz = z < hover_z_ ? 1 : -1;
    int err = dx - dz;
    for (int guard = 0; guard < 400; ++guard) {
      stamp(x, z);
      if (x == hover_x_ && z == hover_z_) break;
      const int e2 = 2 * err;
      if (e2 > -dz) {
        err -= dz;
        x += sx;
      }
      if (e2 < dx) {
        err += dx;
        z += sz;
      }
    }
    rect_x0_ = hover_x_;
    rect_z0_ = hover_z_;
  }
  if (released) {
    if (drag_ == Drag::kRect)
      PaintCells(rect_x0_, rect_z0_, hover_x_, hover_z_, glyph_);
    drag_ = Drag::kNone;
  }
}

// Furniture snaps to a free, non-wall cell. Decor keeps the point hit as an
// offset from the cell center and, when the ray hits a prop first, a lift to
// the top of that prop (decor on a table).
void EditorApp::PlaceInput(const Ray& ray, const bool pressed) {
  if (!pressed || place_model_.empty()) return;
  Vec3 hit;
  int cx, cz;
  const bool ground = RayLevel(ray, &hit, &cx, &cz);
  EntityRef onto;
  float t_entity = 0;
  const bool on_entity = PickEntity(ray, &onto, &t_entity);
  const LevelDoc* doc = project_.Level(level_);
  if (!doc) return;
  Item item;
  item.model = place_model_;
  item.yaw = place_yaw_;
  if (!place_as_decor_) {
    if (!ground) return;
    if (project_.Tiles().Solid(doc->At(cx, cz)) &&
        !project_.Tiles().Thin(doc->At(cx, cz))) {
      Log("Furniture cannot stand in a wall.");
      return;
    }
    for (const auto& f : doc->furniture)
      if (f.x == cx && f.y == cz) {
        Log("A piece of furniture stands there already.");
        return;
      }
    item.x = cx;
    item.y = cz;
    Select(project_.AddFurniture(level_, item));
    return;
  }
  Vec3 p = hit;
  float lift = 0;
  if (on_entity && (!ground || t_entity < Length(hit - ray.o))) {
    Vec3 c, h;
    float yaw;
    EntityBox(onto, &c, &h, &yaw);
    p = ray.At(t_entity);
    lift = (c.y + h.y) - FloorY();
  } else if (!ground) {
    return;
  }
  item.x = static_cast<int>(std::floor(p.x));
  item.y = static_cast<int>(std::floor(p.z));
  item.off_x = SnapValue(p.x - (item.x + 0.5f), false);
  item.off_z = SnapValue(p.z - (item.y + 0.5f), false);
  item.lift = std::round(lift * 1000) / 1000;
  Select(project_.AddDecor(level_, item));
}

// Handle ids in hover_axis_: 0, 1, 2 are the x, y, z arrows, 3 the rotate
// ring, 4 the floor-plane square. Handles are hit-tested in screen space
// (a few pixels), and their length scales with the camera distance so they
// keep a constant on-screen size. A drag stores the pose at the press and
// computes every update from it, so snapping never accumulates drift.
void EditorApp::GizmoInput(const Ray& ray, const ImVec2 mouse,
                           const ImVec2 size) {
  const ImGuiIO& io = ImGui::GetIO();
  Pose pose;
  const bool have = SelectedPose(&pose);
  const bool gizmo = have && (tool_ == Tool::kMove || tool_ == Tool::kRotate);

  hover_axis_ = -1;
  Vec3 origin{};
  float len = 1;
  if (gizmo) {
    origin = WorldPos(pose);
    len = std::clamp(Length(origin - cam_.Eye()) * 0.18f, 0.35f, 6.0f);
    const ImVec2 m = mouse;
    float sx, sy, tx, ty, best = 11.0f;
    if (tool_ == Tool::kMove &&
        cam_.Project(origin, size.x, size.y, &sx, &sy)) {
      const Vec3 dirs[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
      for (int i = 0; i < 3; ++i) {
        if (!cam_.Project(origin + dirs[i] * len, size.x, size.y, &tx, &ty))
          continue;
        const float d = DistToSegment(m, {sx, sy}, {tx, ty});
        if (d < best) {
          best = d;
          hover_axis_ = i;
        }
      }
      if (hover_axis_ < 0 && std::hypot(m.x - sx, m.y - sy) < 9.0f)
        hover_axis_ = 4;
    } else if (tool_ == Tool::kRotate && pose.has_yaw) {
      float px = 0, py = 0;
      bool first = true;
      for (int i = 0; i <= 48; ++i) {
        const float a = 2 * kPi * i / 48;
        float qx, qy;
        if (!cam_.Project(
                origin + Vec3{std::cos(a), 0, std::sin(a)} * (len * 0.9f),
                size.x, size.y, &qx, &qy)) {
          first = true;
          continue;
        }
        if (!first) {
          const float d = DistToSegment(m, {px, py}, {qx, qy});
          if (d < best) {
            best = d;
            hover_axis_ = 3;
          }
        }
        px = qx;
        py = qy;
        first = false;
      }
    }
  }

  hover_entity_valid_ = !gizmo || hover_axis_ < 0
                            ? PickEntity(ray, &hover_entity_, nullptr)
                            : false;

  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt) {
    if (gizmo && hover_axis_ >= 0) {
      drag_pose0_ = pose;
      drag_origin_ = origin;
      if (hover_axis_ <= 2) {
        const Vec3 dirs[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        drag_axis_ = dirs[hover_axis_];
        NearestOnLine(ray, origin, drag_axis_, &drag_t0_);
        drag_ = Drag::kAxis;
      } else if (hover_axis_ == 3) {
        const float t = RayHitsHorizontalPlane(ray, origin.y);
        if (t >= 0) {
          const Vec3 h = ray.At(t);
          drag_angle0_ = std::atan2(h.x - origin.x, h.z - origin.z);
          drag_yaw0_ = static_cast<float>(pose.yaw);
          drag_ = Drag::kRotate;
        }
      } else {
        const float t = RayHitsHorizontalPlane(ray, origin.y);
        if (t >= 0) {
          drag_offset_ = origin - ray.At(t);
          drag_ = Drag::kPlane;
        }
      }
    } else if (hover_entity_valid_) {
      Select(hover_entity_);
      if (tool_ == Tool::kMove) {
        Pose p;
        if (project_.GetPose(Here(), hover_entity_, &p)) {
          const Vec3 o = WorldPos(p);
          const float t = RayHitsHorizontalPlane(ray, o.y);
          if (t >= 0) {
            drag_pose0_ = p;
            drag_origin_ = o;
            drag_offset_ = o - ray.At(t);
            drag_ = Drag::kPlane;
          }
        }
      }
    } else {
      Deselect();
    }
  }

  if (drag_ != Drag::kNone && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
      have) {
    const bool fine = io.KeyCtrl;
    Pose np = drag_pose0_;
    if (drag_ == Drag::kAxis) {
      float t;
      if (NearestOnLine(ray, drag_origin_, drag_axis_, &t)) {
        const float d = t - drag_t0_;
        if (drag_axis_.x != 0)
          np.x = SnapValue(static_cast<float>(drag_pose0_.x) + d, fine);
        if (drag_axis_.z != 0)
          np.z = SnapValue(static_cast<float>(drag_pose0_.z) + d, fine);
        if (drag_axis_.y != 0)
          np.y = SnapValue(static_cast<float>(drag_pose0_.y) + d, fine);
        project_.SetPose(Here(), sel_, np);
      }
    } else if (drag_ == Drag::kPlane) {
      const float t = RayHitsHorizontalPlane(ray, drag_origin_.y);
      if (t >= 0) {
        const Vec3 p = ray.At(t) + drag_offset_;
        np.x = SnapValue(p.x, fine);
        np.z = SnapValue(p.z, fine);
        project_.SetPose(Here(), sel_, np);
      }
    } else if (drag_ == Drag::kRotate) {
      const float t = RayHitsHorizontalPlane(ray, drag_origin_.y);
      if (t >= 0) {
        const Vec3 h = ray.At(t);
        const float a = std::atan2(h.x - drag_origin_.x, h.z - drag_origin_.z);
        float yaw = drag_yaw0_ + (a - drag_angle0_) * 180.0f / kPi;
        if (snap_rot_ && !fine) yaw = std::round(yaw / 15.0f) * 15.0f;
        np.yaw = yaw;
        project_.SetPose(Here(), sel_, np);
      }
    }
  }
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
      (drag_ == Drag::kAxis || drag_ == Drag::kPlane || drag_ == Drag::kRotate))
    drag_ = Drag::kNone;
}

// Camera: middle mouse or Alt + left orbits (Shift pans), right mouse looks
// and flies with W A S D Q E, the wheel zooms. Otherwise the tool handles
// the mouse; the single-key shortcuts apply only while the viewport is
// hovered and no text field has focus.
void EditorApp::ViewportInput(const ImVec2 size, const bool hovered,
                              const bool active) {
  (void)active;
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 mouse{io.MousePos.x - view_origin_.x,
                     io.MousePos.y - view_origin_.y};
  const float aspect = size.x / std::max(1.0f, size.y);
  const Ray ray =
      cam_.PickRay(mouse.x / size.x * 2 - 1, 1 - mouse.y / size.y * 2, aspect);

  if (hovered && !cam_drag_) {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.KeyAlt)) {
      cam_drag_ = true;
      cam_mode_ = io.KeyShift ? 1 : 0;
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      cam_drag_ = true;
      cam_mode_ = 2;
    }
  }
  if (cam_drag_) {
    const bool held = cam_mode_ == 2
                          ? ImGui::IsMouseDown(ImGuiMouseButton_Right)
                          : (ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                             ImGui::IsMouseDown(ImGuiMouseButton_Left));
    if (!held) {
      cam_drag_ = false;
    } else if (cam_mode_ == 0) {
      cam_.Orbit(io.MouseDelta.x * 0.006f, io.MouseDelta.y * 0.006f);
    } else if (cam_mode_ == 1) {
      cam_.Pan(io.MouseDelta.x, io.MouseDelta.y, size.y);
    } else {
      cam_.Look(io.MouseDelta.x * 0.004f, io.MouseDelta.y * 0.004f);
      const float speed = (io.KeyShift ? 14.0f : 4.5f) * io.DeltaTime;
      float f = 0, r = 0, u = 0;
      if (ImGui::IsKeyDown(ImGuiKey_W)) f += speed;
      if (ImGui::IsKeyDown(ImGuiKey_S)) f -= speed;
      if (ImGui::IsKeyDown(ImGuiKey_D)) r += speed;
      if (ImGui::IsKeyDown(ImGuiKey_A)) r -= speed;
      if (ImGui::IsKeyDown(ImGuiKey_E)) u += speed;
      if (ImGui::IsKeyDown(ImGuiKey_Q)) u -= speed;
      cam_.Fly(f, r, u);
    }
  }
  if (hovered && !cam_drag_) {
    if (io.MouseWheel != 0)
      cam_.Zoom(io.MouseWheel * (io.KeyCtrl ? 0.4f : 1.0f));
    if (io.MouseWheelH != 0) cam_.Pan(-io.MouseWheelH * 40.0f, 0, size.y);
  }

  Vec3 hit;
  int cx = 0, cz = 0;
  hover_cell_valid_ = hovered && !cam_drag_ && RayLevel(ray, &hit, &cx, &cz);
  if (hover_cell_valid_) {
    hover_x_ = cx;
    hover_z_ = cz;
    hover_point_ = hit;
  }
  hover_entity_valid_ = false;
  if (!hovered && drag_ == Drag::kNone) {
    hover_axis_ = -1;
    return;
  }
  if (cam_drag_) return;

  const bool alt = io.KeyAlt;
  const bool pressed =
      hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !alt;
  const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
  switch (tool_) {
    case Tool::kSelect:
    case Tool::kMove:
    case Tool::kRotate:
      GizmoInput(ray, mouse, size);
      break;
    case Tool::kPaint:
      PaintInput(ray, pressed, down && !alt, released);
      break;
    case Tool::kPlace:
      PlaceInput(ray, pressed);
      hover_entity_valid_ = PickEntity(ray, &hover_entity_, nullptr);
      break;
  }

  if (hovered && !io.WantTextInput && !(cam_drag_)) {
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
      if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) tool_ = Tool::kSelect;
      if (ImGui::IsKeyPressed(ImGuiKey_W, false)) tool_ = Tool::kMove;
      if (ImGui::IsKeyPressed(ImGuiKey_E, false)) tool_ = Tool::kRotate;
      if (ImGui::IsKeyPressed(ImGuiKey_B, false)) tool_ = Tool::kPaint;
      if (ImGui::IsKeyPressed(ImGuiKey_N, false)) tool_ = Tool::kPlace;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) FocusSelection();
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) FrameLevel();
    if (ImGui::IsKeyPressed(ImGuiKey_T, false)) cam_.TopDown();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      Deselect();
      drag_ = Drag::kNone;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
      DeleteSelected();
    if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false))
      brush_ = std::max(1, brush_ - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false))
      brush_ = std::min(9, brush_ + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
      if (tool_ == Tool::kPlace) {
        place_yaw_ = std::fmod(place_yaw_ + 90.0f, 360.0f);
      } else {
        Pose p;
        if (SelectedPose(&p) && p.has_yaw) {
          p.yaw += 90;
          project_.SetPose(Here(), sel_, p);
        }
      }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) Nudge(-1, 0, 0);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) Nudge(1, 0, 0);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) Nudge(0, -1, 0);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) Nudge(0, 1, 0);
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) Nudge(0, 0, 1);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) Nudge(0, 0, -1);
  }
}

// Everything drawn here is rebuilt each frame from the editor state.
void EditorApp::DrawOverlay(const ImVec2 size) {
  (void)size;
  viewport_.ClearOverlay();
  const LevelDoc* doc = project_.Level(level_);
  if (!doc) return;
  const float fy = FloorY();
  if (show_grid_) {
    const Rgba minor{1, 1, 1, 0.07f}, major{1, 1, 1, 0.16f},
        edge{1, 1, 1, 0.38f};
    for (int x = 0; x <= doc->Width(); ++x)
      viewport_.Line(
          {static_cast<float>(x), fy + 0.003f, 0},
          {static_cast<float>(x), fy + 0.003f,
           static_cast<float>(doc->Height())},
          x == 0 || x == doc->Width() ? edge : (x % 5 == 0 ? major : minor));
    for (int z = 0; z <= doc->Height(); ++z)
      viewport_.Line(
          {0, fy + 0.003f, static_cast<float>(z)},
          {static_cast<float>(doc->Width()), fy + 0.003f,
           static_cast<float>(z)},
          z == 0 || z == doc->Height() ? edge : (z % 5 == 0 ? major : minor));
    viewport_.Line({0, fy + 0.004f, 0}, {1.5f, fy + 0.004f, 0}, kRed);
    viewport_.Line({0, fy + 0.004f, 0}, {0, fy + 0.004f, 1.5f}, kBlue);
  }

  if (show_triggers_) {
    const LevelInfo info = project_.Info(level_);
    for (const Trigger& t : doc->triggers) {
      Vec3 c, h;
      if (t.shape == TriggerShape::kCell) {
        const float y = info.stacked ? t.floor * mythos::kFloorPitch : fy;
        c = {static_cast<float>(t.cell_x) + 0.5f, y + 1.25f,
             static_cast<float>(t.cell_z) + 0.5f};
        h = {0.5f, 1.25f, 0.5f};
      } else {
        c = {static_cast<float>(t.min.x * 0.5 + t.max.x * 0.5),
             static_cast<float>(t.min.y * 0.5 + t.max.y * 0.5),
             static_cast<float>(t.min.z * 0.5 + t.max.z * 0.5)};
        h = {static_cast<float>(t.max.x * 0.5 - t.min.x * 0.5),
             static_cast<float>(t.max.y * 0.5 - t.min.y * 0.5),
             static_cast<float>(t.max.z * 0.5 - t.min.z * 0.5)};
      }
      if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z) ||
          !std::isfinite(h.x) || !std::isfinite(h.y) || !std::isfinite(h.z) ||
          !std::isfinite(c.x + h.x) || !std::isfinite(c.x - h.x) ||
          !std::isfinite(c.y + h.y) || !std::isfinite(c.y - h.y) ||
          !std::isfinite(c.z + h.z) || !std::isfinite(c.z - h.z))
        continue;
      const bool selected = t.id == trigger_sel_;
      viewport_.WireBox(
          c, h, 0, selected ? kYellow : Rgba{0.25f, 0.85f, 1, 0.7f}, selected);
    }
  }

  for (const EntityRef& r : project_.Entities(Here())) {
    Pose p;
    Visual v;
    if (!project_.GetPose(Here(), r, &p) || !VisualOf(r, &v)) continue;
    const Vec3 pos = WorldPos(p);
    const bool selected = has_sel_ && r == sel_;
    const Rgba c{v.color[0], v.color[1], v.color[2], 1};
    if (v.shape == Shape::kLight && show_lights_) {
      const float s = 0.14f;
      viewport_.Line(pos - Vec3{s, 0, 0}, pos + Vec3{s, 0, 0}, c, true);
      viewport_.Line(pos - Vec3{0, s, 0}, pos + Vec3{0, s, 0}, c, true);
      viewport_.Line(pos - Vec3{0, 0, s}, pos + Vec3{0, 0, s}, c, true);
      viewport_.Line(pos, {pos.x, fy, pos.z}, {c.r, c.g, c.b, 0.35f});
      if (selected && v.radius > 0)
        viewport_.Circle(pos, v.radius, {c.r, c.g, c.b, 0.7f}, false);
    } else if (v.shape == Shape::kMarker) {
      viewport_.Circle(pos, 0.16f, c, true, 16);
      viewport_.Line(pos, {pos.x, fy, pos.z}, {c.r, c.g, c.b, 0.3f});
    } else if (v.shape == Shape::kActor) {
      const Vec3 dir{std::cos(Rad(v.facing_deg)), 0,
                     std::sin(Rad(v.facing_deg))};
      viewport_.Arrow(pos + Vec3{0, 0.05f, 0},
                      pos + dir * 1.0f + Vec3{0, 0.05f, 0}, c, 0.25f);
      viewport_.Circle(pos, 0.3f, c, true, 24);
    } else if (v.shape == Shape::kRegion && show_rooms_) {
      Vec3 bc, h;
      float yaw;
      if (EntityBox(r, &bc, &h, &yaw)) {
        viewport_.WireBox(bc, h, 0, {c.r, c.g, c.b, selected ? 0.9f : 0.5f});
        viewport_.Quad({bc.x - h.x, fy + 0.02f, bc.z - h.z},
                       {bc.x + h.x, fy + 0.02f, bc.z - h.z},
                       {bc.x + h.x, fy + 0.02f, bc.z + h.z},
                       {bc.x - h.x, fy + 0.02f, bc.z + h.z},
                       {c.r * 0.8f, c.g * 0.7f, c.b, 0.12f});
        if (selected) viewport_.WireBox(bc, h, 0, kYellow, true);
      }
    }
    if (v.shape == Shape::kRegion) continue;
    Vec3 bc, h;
    float yaw;
    const bool hover = hover_entity_valid_ && r == hover_entity_;
    if ((selected || hover || show_colliders_) && EntityBox(r, &bc, &h, &yaw))
      viewport_.WireBox(
          bc, h, yaw,
          selected ? kYellow : (hover ? kWhite : Rgba{1, 1, 1, 0.25f}),
          selected || hover);
  }

  if (tool_ == Tool::kPaint && hover_cell_valid_) {
    const int b = std::max(1, brush_);
    const int half_brush = (b - 1) / 2;
    const auto x0 = static_cast<float>(hover_x_ - half_brush),
               z0 = static_cast<float>(hover_z_ - half_brush);
    float x1 = x0 + static_cast<float>(b), z1 = z0 + static_cast<float>(b);
    float rx0 = x0, rz0 = z0;
    if (drag_ == Drag::kRect) {
      rx0 = static_cast<float>(std::min(rect_x0_, hover_x_));
      rz0 = static_cast<float>(std::min(rect_z0_, hover_z_));
      x1 = static_cast<float>(std::max(rect_x0_, hover_x_) + 1);
      z1 = static_cast<float>(std::max(rect_z0_, hover_z_) + 1);
    }
    const float y = fy + 0.012f;
    viewport_.Quad({rx0, y, rz0}, {x1, y, rz0}, {x1, y, z1}, {rx0, y, z1},
                   {0.3f, 0.9f, 0.4f, 0.28f});
    viewport_.Line({rx0, y, rz0}, {x1, y, rz0}, kGreen, true);
    viewport_.Line({x1, y, rz0}, {x1, y, z1}, kGreen, true);
    viewport_.Line({x1, y, z1}, {rx0, y, z1}, kGreen, true);
    viewport_.Line({rx0, y, z1}, {rx0, y, rz0}, kGreen, true);
  }
  if (tool_ == Tool::kPlace && hover_cell_valid_) {
    Vec3 lo, hi;
    ModelBounds(place_model_, &lo, &hi);
    Vec3 base = place_as_decor_ ? hover_point_
                                : Vec3{hover_x_ + 0.5f, fy, hover_z_ + 0.5f};
    const Vec3 c = (lo + hi) * 0.5f;
    const float cs = std::cos(Rad(place_yaw_)), sn = std::sin(Rad(place_yaw_));
    const Vec3 centre =
        base + Vec3{c.x * cs + c.z * sn, c.y, -c.x * sn + c.z * cs};
    viewport_.SolidBox(centre, (hi - lo) * 0.5f, BoxYaw(place_yaw_),
                       {0.3f, 0.8f, 1, 0.3f});
    viewport_.WireBox(centre, (hi - lo) * 0.5f, BoxYaw(place_yaw_),
                      {0.4f, 0.85f, 1, 1}, true);
  }

  Pose pose;
  if (SelectedPose(&pose) && (tool_ == Tool::kMove || tool_ == Tool::kRotate)) {
    const Vec3 o = WorldPos(pose);
    const float len = std::clamp(Length(o - cam_.Eye()) * 0.18f, 0.35f, 6.0f);
    auto lit = [&](const Rgba c, const bool on) {
      return on ? Rgba{1, 1, 0.45f, 1} : c;
    };
    if (tool_ == Tool::kMove) {
      viewport_.Arrow(o, o + Vec3{len, 0, 0},
                      lit(kRed, hover_axis_ == 0 || (drag_ == Drag::kAxis &&
                                                     drag_axis_.x != 0)),
                      len * 0.16f);
      viewport_.Arrow(o, o + Vec3{0, len, 0},
                      lit(kGreen, hover_axis_ == 1 || (drag_ == Drag::kAxis &&
                                                       drag_axis_.y != 0)),
                      len * 0.16f);
      viewport_.Arrow(o, o + Vec3{0, 0, len},
                      lit(kBlue, hover_axis_ == 2 || (drag_ == Drag::kAxis &&
                                                      drag_axis_.z != 0)),
                      len * 0.16f);
      const float q = len * 0.22f;
      viewport_.Quad(o + Vec3{0, 0.01f, 0}, o + Vec3{q, 0.01f, 0},
                     o + Vec3{q, 0.01f, q}, o + Vec3{0, 0.01f, q},
                     {1, 1, 0.4f, hover_axis_ == 4 ? 0.7f : 0.35f});
    } else if (pose.has_yaw) {
      viewport_.Circle(o, len * 0.9f,
                       lit({0.95f, 0.75f, 0.2f, 1},
                           hover_axis_ == 3 || drag_ == Drag::kRotate),
                       true);
      const float a = Rad(static_cast<float>(pose.yaw));
      viewport_.Arrow(o, o + Vec3{std::sin(a), 0, std::cos(a)} * (len * 0.9f),
                      {0.95f, 0.75f, 0.2f, 1}, len * 0.12f);
    }
  }
}

// Labels of the selected, hovered (or, if enabled, all) entities, the axis
// widget in the bottom right corner, and the status line.
void EditorApp::DrawScreenOverlay(const ImVec2 origin, const ImVec2 size) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImU32 text = IM_COL32(230, 230, 235, 255),
              dim = IM_COL32(150, 155, 165, 255);
  auto label = [&](const Vec3 p, const std::string& s, const ImU32 col) {
    float sx, sy;
    if (!cam_.Project(p, size.x, size.y, &sx, &sy)) return;
    const ImVec2 sz = ImGui::CalcTextSize(s.c_str());
    const ImVec2 a{origin.x + sx - sz.x / 2 - 3, origin.y + sy - sz.y - 6};
    dl->AddRectFilled(a, {a.x + sz.x + 6, a.y + sz.y + 2},
                      IM_COL32(15, 16, 20, 190), 3);
    dl->AddText({a.x + 3, a.y + 1}, col, s.c_str());
  };
  if (show_labels_ || has_sel_ || hover_entity_valid_) {
    for (const EntityRef& r : project_.Entities(Here())) {
      const bool selected = has_sel_ && r == sel_;
      const bool hover = hover_entity_valid_ && r == hover_entity_;
      if (!show_labels_ && !selected && !hover) continue;
      Vec3 c, h;
      float yaw;
      if (!EntityBox(r, &c, &h, &yaw)) continue;
      label(c + Vec3{0, h.y, 0}, project_.Label(Here(), r),
            selected ? IM_COL32(255, 210, 60, 255) : text);
    }
  }

  const ImVec2 c{origin.x + size.x - 56, origin.y + size.y - 56};
  const OrbitCamera& cam = cam_;
  const Vec3 f = cam.Forward(), r = cam.Right(), u = Cross(r, f);
  const struct {
    Vec3 d;
    ImU32 col;
    const char* name;
  } axes[3] = {{{1, 0, 0}, IM_COL32(240, 80, 80, 255), "X"},
               {{0, 1, 0}, IM_COL32(110, 220, 90, 255), "Y"},
               {{0, 0, 1}, IM_COL32(90, 140, 255, 255), "Z"}};
  dl->AddCircleFilled(c, 40, IM_COL32(20, 22, 28, 120), 32);
  for (const auto& a : axes) {
    const ImVec2 e{c.x + Dot(a.d, r) * 28, c.y - Dot(a.d, u) * 28};
    dl->AddLine(c, e, a.col, 2.5f);
    dl->AddCircleFilled(e, 8, a.col, 16);
    dl->AddText({e.x - 3.5f, e.y - 7}, IM_COL32(15, 15, 15, 255), a.name);
  }

  const LevelDoc* doc = project_.Level(level_);
  std::string s = std::string(tool_ == Tool::kSelect   ? "Select"
                              : tool_ == Tool::kMove   ? "Move"
                              : tool_ == Tool::kRotate ? "Rotate"
                              : tool_ == Tool::kPaint  ? "Paint"
                                                       : "Place");
  if (doc && hover_cell_valid_) {
    const char g = doc->At(hover_x_, hover_z_);
    s += "   cell " + std::to_string(hover_x_) + ", " +
         std::to_string(hover_z_) + "  '" + std::string(1, g) + "'";
  }
  char buf[64];
  std::snprintf(buf, sizeof buf, "   %.0f fps", fps_);
  s += buf;
  dl->AddText({origin.x + 10, origin.y + size.y - 24}, dim, s.c_str());
}

// The picture is read back from the game's PPM and uploaded as a texture.
void EditorApp::PollGamePreview() {
  if (!preview_running_ || !preview_job_.valid()) return;
  if (preview_job_.wait_for(std::chrono::seconds(0)) !=
      std::future_status::ready)
    return;
  const bool ok = preview_job_.get();
  preview_running_ = false;
  int w, h;
  std::vector<uint8_t> rgba;
  if (ok && ReadPpm(preview_ppm_, &w, &h, &rgba)) {
    if (!preview_tex_) gl::GenTextures(1, &preview_tex_);
    gl::BindTexture(gl::kTexture2D, preview_tex_);
    gl::TexImage2D(gl::kTexture2D, 0, gl::kRgba8, w, h, 0, gl::kRgba,
                   gl::kUnsignedByte, rgba.data());
    gl::TexParameteri(gl::kTexture2D, gl::kTextureMinFilter, gl::kLinear);
    gl::TexParameteri(gl::kTexture2D, gl::kTextureMagFilter, gl::kLinear);
    gl::TexParameteri(gl::kTexture2D, gl::kTextureWrapS, gl::kClampToEdge);
    gl::TexParameteri(gl::kTexture2D, gl::kTextureWrapT, gl::kClampToEdge);
    preview_w_ = w;
    preview_h_ = h;
    preview_log_ = "Rendered by the game.";
  } else {
    preview_log_ =
        "The game did not produce a picture (is it built? is the display "
        "awake?).";
  }
  Log(preview_log_);
}

// Builds a throwaway asset folder in the temp directory: the unsaved levels
// and data files written out, everything else of the stage linked in (or
// copied where symlinks are not allowed). The game then runs on it with the
// adapter's command line, in a worker thread, as a blocking system() call.
void EditorApp::RunGamePreview() {
  if (preview_running_) return;
  if (game_exe_.empty()) game_exe_ = FindGame();
  if (game_exe_.empty()) {
    const char* base = SDL_GetBasePath();
    preview_log_ = project_.adapter().Name() +
                   " was not found next to the editor (looked from " +
                   (base ? base : "?") + "): build it, or pass --game PATH.";
    Log(preview_log_);
    return;
  }
  std::error_code ec;
  const fs::path dir = fs::temp_directory_path(ec) / "mythos_editor_preview";
  fs::remove_all(dir, ec);
  fs::create_directories(dir / "levels", ec);
  fs::create_directories(dir / "data", ec);

  PreviewRequest req;
  req.where = Here();
  req.x = cam_.target.x;
  req.z = cam_.target.z;
  req.heading_deg = cam_.heading * 180.0f / kPi;
  req.options = preview_options_;
  req.out_ppm = (dir / "shot.ppm").string();
  req.asset_dir = dir.string();
  DataSet data = project_.data;
  std::vector<std::string> args;
  if (!project_.adapter().Preview(data, req, &args)) {
    preview_log_ = "This level cannot be previewed in the game.";
    Log(preview_log_);
    return;
  }

  const fs::path stage = fs::absolute(args_.stage_dir, ec);
  auto link_or_copy = [&](const fs::path& from, const fs::path& to) {
    std::error_code e;
    fs::create_symlink(from, to, e);
    if (e) {
      e.clear();
      if (fs::is_directory(from))
        fs::copy(from, to, fs::copy_options::recursive, e);
      else
        fs::copy_file(from, to, e);
    }
  };
  for (const auto& e : fs::directory_iterator(stage, ec))
    if (e.path().filename() != "levels" && e.path().filename() != "data")
      link_or_copy(e.path(), dir / e.path().filename());
  for (const auto& e : fs::directory_iterator(stage / "data", ec)) {
    const std::string n = e.path().stem().string();
    if (e.path().extension() != ".json" || !data.contains(n))
      link_or_copy(e.path(), dir / "data" / e.path().filename());
  }
  for (const auto& e : fs::directory_iterator(stage / "levels", ec))
    if (e.path().extension() != ".txt")
      link_or_copy(e.path(), dir / "levels" / e.path().filename());
  for (const auto& name : project_.LevelNames())
    mythos::WriteFileAtomic((dir / "levels" / (name + ".txt")).string(),
                            SerializeLevelDoc(*project_.Level(name)));
  for (const auto& [name, v] : data)
    mythos::WriteFileAtomic((dir / "data" / (name + ".json")).string(),
                            v.dump(2) + "\n");

  preview_ppm_ = req.out_ppm;
  std::string line = "\"" + game_exe_ + "\"";
  for (const std::string& a : args) line += " \"" + a + "\"";
#ifdef _WIN32
  line = "\"" + line + "\"";
#endif
  preview_running_ = true;
  preview_log_ = "Running the game...";
  preview_job_ = std::async(std::launch::async, [line] {
    // The command is built from the user's own preview settings.
    // NOLINTNEXTLINE(bugprone-command-processor)
    const int status = std::system(line.c_str());
    return status == 0;
  });
}
}
