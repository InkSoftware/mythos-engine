#include "engine/editor/project.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <memory>

#include "engine/core/fileio.h"

namespace mythos::editor {
namespace fs = std::filesystem;

namespace {
// The adapter used when none is given: no game data.
const Adapter& NullAdapter() {
  static const Adapter null_adapter;
  return null_adapter;
}

// Rounds to millimeters, so offsets saved in level files stay tidy.
double Round3(const double v) { return std::round(v * 1000) / 1000; }

bool ListIndexOk(const std::vector<Item>& list, const int index) {
  return index >= 0 && static_cast<size_t>(index) < list.size();
}
}

Project::Project(const Adapter* const adapter)
    : adapter_(adapter ? adapter : &NullAdapter()) {}

bool Project::Load(const std::string& root, std::string* const error) {
  root_ = root;
  levels_.clear();
  data.clear();
  dirty_levels_.clear();
  dirty_data_ = false;
  const std::string dir = AssetDir();
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(dir + "/levels", ec)) {
    if (e.path().extension() != ".txt") continue;
    LevelDoc doc;
    std::string err;
    if (!ParseLevelDoc(ReadTextFile(e.path().string()), &doc, &err)) {
      if (error) *error = e.path().filename().string() + ": " + err;
      return false;
    }
    doc.name = e.path().stem().string();
    levels_[doc.name] = std::move(doc);
  }
  if (levels_.empty()) {
    if (error) *error = "no levels in " + dir + "/levels";
    return false;
  }
  json::Value tiles;
  auto tileset = std::make_shared<TileSet>();
  if (!json::ParseFile(dir + "/data/tiles.json", tiles, error) ||
      !ParseTileSet(tiles, *tileset, error))
    return false;
  tiles_ = tileset;
  for (const std::string& name : adapter_->DataFiles()) {
    std::string path = dir;
    path += "/data/";
    path += name;
    path += ".json";
    if (!json::ParseFile(path, data[name], error)) return false;
  }
  return true;
}

bool Project::Save(std::string* const error) {
  for (const auto& name : dirty_levels_) {
    const auto it = levels_.find(name);
    if (it == levels_.end()) continue;
    if (!WriteFileAtomic(AssetDir() + "/levels/" + name + ".txt",
                         SerializeLevelDoc(it->second))) {
      if (error) *error = "cannot write levels/" + name + ".txt";
      return false;
    }
  }
  if (dirty_data_) {
    for (const auto& [name, v] : data) {
      const std::string path = AssetDir() + "/data/" + name + ".json";
      const std::string text = v.dump(2) + "\n";
      if (text == ReadTextFile(path)) continue;
      if (!WriteFileAtomic(path, text)) {
        if (error) *error = "cannot write data/" + name + ".json";
        return false;
      }
    }
  }
  dirty_levels_.clear();
  dirty_data_ = false;
  return true;
}

std::vector<std::string> Project::LevelNames() const {
  std::vector<std::string> out;
  out.reserve(levels_.size());
  for (const auto& [name, doc] : levels_) out.push_back(name);
  return out;
}

LevelDoc* Project::Level(const std::string& name) {
  const auto it = levels_.find(name);
  return it == levels_.end() ? nullptr : &it->second;
}

const LevelDoc* Project::Level(const std::string& name) const {
  const auto it = levels_.find(name);
  return it == levels_.end() ? nullptr : &it->second;
}

std::vector<std::pair<int, std::string>> Project::Stack(
    const std::string& context, const std::string& level) const {
  std::vector<std::pair<int, std::string>> out;
  const LevelInfo mine = Info(level);
  for (const auto& [floor, name] : adapter_->Stack(data, {level, context})) {
    if (name.empty() || !Level(name)) continue;
    out.emplace_back(floor, floor == mine.floor ? level : name);
  }
  // A stacked level always appears in its own stack, even if the adapter left
  // it out.
  if (!out.empty() && mine.stacked &&
      std::ranges::none_of(
          out, [&](const auto& p) { return p.first == mine.floor; }))
    out.emplace_back(mine.floor, level);
  std::ranges::sort(out);
  return out;
}

bool Project::CreateLevel(const std::string& name, const int w, const int h) {
  if (name.empty() || levels_.contains(name) || w < 3 || h < 3 || w > 200 ||
      h > 200)
    return false;
  for (char c : name)
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
      return false;
  LevelDoc doc;
  doc.name = name;
  doc.rows.assign(static_cast<size_t>(h),
                  std::string(static_cast<size_t>(w), '.'));
  for (int x = 0; x < w; ++x) doc.rows[0][x] = doc.rows[h - 1][x] = '#';
  for (int y = 0; y < h; ++y) doc.rows[y][0] = doc.rows[y][w - 1] = '#';
  levels_[name] = std::move(doc);
  Touch(name);
  return true;
}

std::vector<char> Project::Glyphs() const {
  std::vector<char> out = {'.'};
  for (int c = 33; c < 127; ++c) {
    const TileInfo& t = (*tiles_)[static_cast<char>(c)];
    // Printable ASCII only (33..126; space is not a glyph). Glyphs with no
    // behavior or look in the tile set are left out.
    const bool named = t.solid || t.door != DoorKind::kNone || t.thin ||
                       t.window || t.open_ceiling || t.open_floor || t.empty ||
                       !t.wall.empty() || !t.floor.empty() || !t.sprite.empty();
    if (named && c != '.') out.push_back(static_cast<char>(c));
  }
  return out;
}

std::vector<EntityRef> Project::Entities(const Where& w) const {
  std::vector<EntityRef> out;
  if (const LevelDoc* doc = Level(w.level)) {
    for (size_t i = 0; i < doc->furniture.size(); ++i)
      out.push_back({kFurniture, static_cast<int>(i), ""});
    for (size_t i = 0; i < doc->decor.size(); ++i)
      out.push_back({kDecor, static_cast<int>(i), ""});
  }
  for (EntityRef& r : adapter_->Entities(data, w)) out.push_back(std::move(r));
  return out;
}

json::Value* Project::Data(const Where& w, const EntityRef& r) {
  if (r.kind < kFirstGameKind) return nullptr;
  return adapter_->Data(data, w, r);
}

bool Project::Describe(const Where& w, const EntityRef& r,
                       Visual* const out) const {
  if (r.kind == kFurniture || r.kind == kDecor) {
    const LevelDoc* doc = Level(w.level);
    if (!doc) return false;
    const auto& list = r.kind == kFurniture ? doc->furniture : doc->decor;
    if (!ListIndexOk(list, r.index)) return false;
    Visual v;
    v.model = list[static_cast<size_t>(r.index)].model;
    *out = std::move(v);
    return true;
  }
  return adapter_->Describe(data, w, r, out);
}

bool Project::GetPose(const Where& w, const EntityRef& r,
                      Pose* const out) const {
  if (r.kind == kFurniture || r.kind == kDecor) {
    const LevelDoc* doc = Level(w.level);
    if (!doc) return false;
    const auto& list = r.kind == kFurniture ? doc->furniture : doc->decor;
    if (!ListIndexOk(list, r.index)) return false;
    const Item& i = list[static_cast<size_t>(r.index)];
    *out = {i.x + 0.5 + i.off_x, i.lift, i.y + 0.5 + i.off_z, i.yaw, true};
    return true;
  }
  return adapter_->GetPose(data, w, r, out);
}

bool Project::SetPose(const Where& w, const EntityRef& r, const Pose& p) {
  if (r.kind == kFurniture || r.kind == kDecor) {
    LevelDoc* doc = Level(w.level);
    if (!doc) return false;
    auto& list = r.kind == kFurniture ? doc->furniture : doc->decor;
    if (!ListIndexOk(list, r.index)) return false;
    Item& i = list[static_cast<size_t>(r.index)];
    const int cx = std::clamp(static_cast<int>(std::floor(p.x)), 0,
                              std::max(0, doc->Width() - 1));
    const int cy = std::clamp(static_cast<int>(std::floor(p.z)), 0,
                              std::max(0, doc->Height() - 1));
    i.x = cx;
    i.y = cy;
    if (r.kind == kDecor) {
      i.off_x = Round3(p.x - (cx + 0.5));
      i.off_z = Round3(p.z - (cy + 0.5));
      i.lift = Round3(std::max(0.0, p.y));
    }
    i.yaw = std::fmod(std::fmod(p.yaw, 360.0) + 360.0, 360.0);
    Touch(w.level);
    return true;
  }
  if (!adapter_->SetPose(data, w, r, p)) return false;
  TouchData();
  return true;
}

std::string Project::Label(const Where& w, const EntityRef& r) const {
  if (r.kind == kFurniture || r.kind == kDecor) {
    const LevelDoc* doc = Level(w.level);
    if (!doc) return "?";
    const auto& list = r.kind == kFurniture ? doc->furniture : doc->decor;
    if (!ListIndexOk(list, r.index)) return "?";
    const Item& i = list[static_cast<size_t>(r.index)];
    return i.model + "  (" + std::to_string(i.x) + ", " + std::to_string(i.y) +
           ")";
  }
  return adapter_->Label(data, w, r);
}

std::string Project::KindName(const int kind) const {
  if (kind == kFurniture) return "Furniture";
  if (kind == kDecor) return "Decor";
  return adapter_->KindName(kind);
}

EntityRef Project::AddFurniture(const std::string& level, const Item& item) {
  LevelDoc* doc = Level(level);
  if (!doc) return {kFurniture, -1, ""};
  Item i = item;
  i.off_x = i.off_z = i.lift = 0;
  doc->furniture.push_back(i);
  Touch(level);
  return {kFurniture, static_cast<int>(doc->furniture.size()) - 1, ""};
}

EntityRef Project::AddDecor(const std::string& level, const Item& item) {
  LevelDoc* doc = Level(level);
  if (!doc) return {kDecor, -1, ""};
  doc->decor.push_back(item);
  Touch(level);
  return {kDecor, static_cast<int>(doc->decor.size()) - 1, ""};
}

EntityRef Project::Duplicate(const Where& w, const EntityRef& r) {
  EntityRef none{r.kind, -1, ""};
  if (r.kind == kFurniture || r.kind == kDecor) {
    LevelDoc* doc = Level(w.level);
    if (!doc) return none;
    auto& list = r.kind == kFurniture ? doc->furniture : doc->decor;
    if (!ListIndexOk(list, r.index)) return none;
    Item i = list[static_cast<size_t>(r.index)];
    if (r.kind == kFurniture) {
      for (int d = 1; d < doc->Width(); ++d) {
        const int nx = i.x + d;
        if (!doc->InBounds(nx, i.y)) break;
        const bool taken = std::ranges::any_of(
            list, [&](const Item& o) { return o.x == nx && o.y == i.y; });
        if (!taken && !Tiles().Solid(doc->At(nx, i.y))) {
          i.x = nx;
          break;
        }
      }
    } else {
      i.off_x += 0.25;
    }
    list.push_back(i);
    Touch(w.level);
    return {r.kind, static_cast<int>(list.size()) - 1, ""};
  }
  const EntityRef copy = adapter_->Duplicate(data, w, r);
  if (copy.index >= 0 || !copy.id.empty()) TouchData();
  return copy;
}

bool Project::Remove(const Where& w, const EntityRef& r) {
  if (r.kind == kFurniture || r.kind == kDecor) {
    LevelDoc* doc = Level(w.level);
    if (!doc) return false;
    auto& list = r.kind == kFurniture ? doc->furniture : doc->decor;
    if (!ListIndexOk(list, r.index)) return false;
    list.erase(list.begin() + r.index);
    Touch(w.level);
    return true;
  }
  if (!adapter_->Remove(data, w, r)) return false;
  TouchData();
  return true;
}

std::vector<Problem> Project::Check(
    const Where& w, const std::vector<std::string>& model_names) const {
  std::vector<Problem> out;
  const LevelDoc* doc = Level(w.level);
  if (!doc) return out;
  auto has_model = [&](const std::string& m) {
    return std::ranges::find(model_names, m) != model_names.end();
  };
  auto add = [&](const std::string& text, const EntityRef& ref, const double x,
                 const double z) {
    out.push_back({text, ref, ref.index >= 0 || !ref.id.empty(), x, z});
  };
  auto in_wall = [&](const int x, const int y) {
    return Tiles().Solid(doc->At(x, y)) && !Tiles().Thin(doc->At(x, y)) &&
           !Tiles().IsDoor(doc->At(x, y));
  };
  for (size_t n = 0; n < doc->furniture.size(); ++n) {
    const Item& i = doc->furniture[n];
    const EntityRef ref{kFurniture, static_cast<int>(n), ""};
    if (!doc->InBounds(i.x, i.y))
      add("furniture '" + i.model + "' is outside the map", ref, i.x, i.y);
    else if (in_wall(i.x, i.y))
      add("furniture '" + i.model + "' (" + std::to_string(i.x) + "," +
              std::to_string(i.y) + ") is inside a wall",
          ref, i.x + 0.5, i.y + 0.5);
    if (!has_model(i.model))
      add("furniture model '" + i.model + "' does not exist", ref, i.x + 0.5,
          i.y + 0.5);
  }
  for (size_t n = 0; n < doc->decor.size(); ++n) {
    const Item& i = doc->decor[n];
    const EntityRef ref{kDecor, static_cast<int>(n), ""};
    if (!doc->InBounds(i.x, i.y))
      add("decor '" + i.model + "' is outside the map", ref, i.x, i.y);
    if (!has_model(i.model))
      add("decor model '" + i.model + "' does not exist", ref, i.x + 0.5,
          i.y + 0.5);
  }
  for (const EntityRef& r : adapter_->Entities(data, w)) {
    Visual v;
    Pose p;
    if (!adapter_->Describe(data, w, r, &v) ||
        !adapter_->GetPose(data, w, r, &p) || v.shape == Shape::kRegion ||
        v.shape == Shape::kActor)
      continue;
    const std::string name = adapter_->Label(data, w, r);
    const int cx = static_cast<int>(std::floor(p.x)),
              cy = static_cast<int>(std::floor(p.z));
    if (!doc->InBounds(cx, cy))
      add(name + " is outside the map", r, p.x, p.z);
    else if (v.wall_check && in_wall(cx, cy))
      add(name + " is inside a wall", r, p.x, p.z);
  }

  const LevelInfo info = Info(w.level);
  if (info.stacked) {
    for (const auto& [floor, other_name] : Stack(w.context, w.level)) {
      const bool above = floor == info.floor + 1,
                 below = floor == info.floor - 1;
      const LevelDoc* other = Level(other_name);
      if (!other || (!above && !below)) continue;
      for (int y = 0; y < doc->Height(); ++y)
        for (int x = 0; x < doc->Width(); ++x) {
          const auto& mine = Tiles()[doc->At(x, y)];
          const auto& theirs = Tiles()[other->At(x, y)];
          // A stairwell needs a hole in the floor of the level above it, and a
          // hole in the floor needs a stairwell in the level below.
          if (above && mine.open_ceiling && !theirs.open_floor)
            add("stairwell (" + std::to_string(x) + "," + std::to_string(y) +
                    ") has no hole in the floor above",
                {kFurniture, -1, ""}, x + 0.5, y + 0.5);
          if (below && mine.open_floor && !theirs.open_ceiling)
            add("stair hole (" + std::to_string(x) + "," + std::to_string(y) +
                    ") has no stairwell below",
                {kFurniture, -1, ""}, x + 0.5, y + 0.5);
        }
    }
  }
  adapter_->Check(data, w, &out);
  return out;
}

void Project::Touch(const std::string& level) {
  dirty_levels_.insert(level);
  ++revision_;
}

void Project::TouchData() {
  dirty_data_ = true;
  ++revision_;
}

Snapshot Project::Take() const { return {levels_, data}; }

// Marks dirty only the levels and data that differ from the current state.
void Project::Restore(const Snapshot& s) {
  for (const auto& [name, doc] : s.levels) {
    const auto it = levels_.find(name);
    if (it == levels_.end() || !(it->second == doc)) dirty_levels_.insert(name);
  }
  levels_ = s.levels;
  if (!(data == s.data)) dirty_data_ = true;
  data = s.data;
  ++revision_;
}
}
