#include "engine/world/level.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>

#include "engine/core/vfs.h"

namespace mythos {
namespace {
// Revisions come from one counter so two levels never share a stamp.
uint64_t NextRevision() {
  static uint64_t counter = 0;
  return ++counter;
}

// Sampling interval of LineOfSight, in world units; below the thin wall
// thickness so slabs are not stepped over.
constexpr double kSightStep = 0.15;

std::string Trimmed(const std::string& text) {
  const size_t first = text.find_first_not_of(" \t\r");
  if (first == std::string::npos) return "";
  const size_t last = text.find_last_not_of(" \t\r");
  return text.substr(first, last - first + 1);
}

bool ReadFile(const std::string& path, std::string& out) {
  return vfs::ReadFile(path, out);
}
}

bool ValidDecal(const Decal& d) {
  const bool face =
      d.face == DecalFace::kNorth || d.face == DecalFace::kSouth ||
      d.face == DecalFace::kWest || d.face == DecalFace::kEast ||
      d.face == DecalFace::kFloor || d.face == DecalFace::kCeiling ||
      d.face == DecalFace::kThinNorth || d.face == DecalFace::kThinSouth ||
      d.face == DecalFace::kThinWest || d.face == DecalFace::kThinEast ||
      d.face == DecalFace::kGlassNorth || d.face == DecalFace::kGlassSouth ||
      d.face == DecalFace::kGlassWest || d.face == DecalFace::kGlassEast;
  return face && !d.texture.empty() &&
         std::none_of(d.texture.begin(), d.texture.end(),
                      [](unsigned char c) { return std::isspace(c) != 0; }) &&
         std::isfinite(d.offset_u) && std::isfinite(d.offset_v) &&
         std::isfinite(d.width) && d.width > 0 && std::isfinite(d.height) &&
         d.height > 0 && std::isfinite(d.rotation);
}

bool ParseDecalLine(const std::string& line, Decal* const out) {
  if (!out) return false;
  std::istringstream in(line);
  in.imbue(std::locale::classic());
  Decal d;
  std::string face, extra;
  if (!(in >> d.x >> d.y >> face >> d.offset_u >> d.offset_v >> d.width >>
        d.height >> d.rotation >> d.texture) ||
      (in >> extra))
    return false;
  if (face == "north")
    d.face = DecalFace::kNorth;
  else if (face == "south")
    d.face = DecalFace::kSouth;
  else if (face == "west")
    d.face = DecalFace::kWest;
  else if (face == "east")
    d.face = DecalFace::kEast;
  else if (face == "floor")
    d.face = DecalFace::kFloor;
  else if (face == "ceiling")
    d.face = DecalFace::kCeiling;
  else if (face == "thin_north")
    d.face = DecalFace::kThinNorth;
  else if (face == "thin_south")
    d.face = DecalFace::kThinSouth;
  else if (face == "thin_west")
    d.face = DecalFace::kThinWest;
  else if (face == "thin_east")
    d.face = DecalFace::kThinEast;
  else if (face == "glass_north")
    d.face = DecalFace::kGlassNorth;
  else if (face == "glass_south")
    d.face = DecalFace::kGlassSouth;
  else if (face == "glass_west")
    d.face = DecalFace::kGlassWest;
  else if (face == "glass_east")
    d.face = DecalFace::kGlassEast;
  else
    return false;
  if (!ValidDecal(d)) return false;
  *out = std::move(d);
  return true;
}

std::string SerializeDecalLine(const Decal& d) {
  if (!ValidDecal(d)) return {};
  const char* face = "";
  switch (d.face) {
    case DecalFace::kNorth:
      face = "north";
      break;
    case DecalFace::kSouth:
      face = "south";
      break;
    case DecalFace::kWest:
      face = "west";
      break;
    case DecalFace::kEast:
      face = "east";
      break;
    case DecalFace::kFloor:
      face = "floor";
      break;
    case DecalFace::kCeiling:
      face = "ceiling";
      break;
    case DecalFace::kThinNorth:
      face = "thin_north";
      break;
    case DecalFace::kThinSouth:
      face = "thin_south";
      break;
    case DecalFace::kThinWest:
      face = "thin_west";
      break;
    case DecalFace::kThinEast:
      face = "thin_east";
      break;
    case DecalFace::kGlassNorth:
      face = "glass_north";
      break;
    case DecalFace::kGlassSouth:
      face = "glass_south";
      break;
    case DecalFace::kGlassWest:
      face = "glass_west";
      break;
    case DecalFace::kGlassEast:
      face = "glass_east";
      break;
  }
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(std::numeric_limits<double>::max_digits10) << d.x
      << ' ' << d.y << ' ' << face << ' ' << d.offset_u << ' ' << d.offset_v
      << ' ' << d.width << ' ' << d.height << ' ' << d.rotation << ' '
      << d.texture;
  return out.str();
}

bool ParseTriggerLine(const std::string& line, Trigger* const out) {
  if (!out) return false;
  std::istringstream in(line);
  in.imbue(std::locale::classic());
  Trigger t;
  std::string shape, event, once, extra;
  if (!(in >> t.id >> shape >> event >> once)) return false;
  if (shape == "box") {
    t.shape = TriggerShape::kBox;
    if (!(in >> t.min.x >> t.min.y >> t.min.z >> t.max.x >> t.max.y >> t.max.z))
      return false;
  } else if (shape == "cell") {
    t.shape = TriggerShape::kCell;
    if (!(in >> t.cell_x >> t.cell_z >> t.floor)) return false;
  } else {
    return false;
  }
  if (event == "enter")
    t.event = TriggerEvent::kEnter;
  else if (event == "exit")
    t.event = TriggerEvent::kExit;
  else if (event == "stay")
    t.event = TriggerEvent::kStay;
  else if (event == "use")
    t.event = TriggerEvent::kUse;
  else
    return false;
  if (once == "1")
    t.once = true;
  else if (once != "0")
    return false;
  if ((in >> extra) || SerializeTriggerLine(t).empty()) return false;
  *out = std::move(t);
  return true;
}

std::string SerializeTriggerLine(const Trigger& t) {
  if (!ValidTrigger(t) || t.id.front() == '[' || t.id.front() == ';' ||
      std::any_of(t.id.begin(), t.id.end(), [](unsigned char c) {
        return std::isspace(c) != 0 || std::iscntrl(c) != 0;
      }))
    return {};
  const char* event = "";
  switch (t.event) {
    case TriggerEvent::kEnter:
      event = "enter";
      break;
    case TriggerEvent::kExit:
      event = "exit";
      break;
    case TriggerEvent::kStay:
      event = "stay";
      break;
    case TriggerEvent::kUse:
      event = "use";
      break;
  }
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(std::numeric_limits<double>::max_digits10) << t.id
      << (t.shape == TriggerShape::kBox ? " box " : " cell ") << event << ' '
      << (t.once ? 1 : 0);
  if (t.shape == TriggerShape::kBox)
    out << ' ' << t.min.x << ' ' << t.min.y << ' ' << t.min.z << ' ' << t.max.x
        << ' ' << t.max.y << ' ' << t.max.z;
  else
    out << ' ' << t.cell_x << ' ' << t.cell_z << ' ' << t.floor;
  return out.str();
}

bool Level::Parse(const std::vector<std::string>& rows,
                  std::string* const error) {
  if (rows.empty()) {
    if (error) *error = "map is empty";
    return false;
  }
  const size_t width = rows[0].size();
  for (size_t i = 0; i < rows.size(); ++i) {
    if (rows[i].size() != width) {
      if (error) {
        *error = "row " + std::to_string(i) + " has a different length";
      }
      return false;
    }
  }
  w_ = static_cast<int>(width);
  h_ = static_cast<int>(rows.size());
  cells_.clear();
  cells_.reserve(static_cast<size_t>(w_) * static_cast<size_t>(h_));
  for (const auto& row : rows) {
    cells_.insert(cells_.end(), row.begin(), row.end());
  }
  solid_.assign(cells_.size(), 0);
  furniture_.clear();
  decals_.clear();
  triggers_.clear();
  revision_ = NextRevision();
  return true;
}

bool Level::ParseText(const std::string& text, std::string* const error) {
  std::vector<std::string> rows;
  std::vector<Placement> furniture, decor;
  std::vector<Decal> decals;
  std::vector<Trigger> triggers;
  std::set<std::string> trigger_ids;
  std::string section;
  std::istringstream in(text);
  std::string raw;
  while (std::getline(in, raw)) {
    const std::string line = Trimmed(raw);
    if (line.empty() || line[0] == ';') continue;
    if (line.front() == '[') {
      section = line;
      continue;
    }
    if (section == "[map]") {
      rows.push_back(line);
    } else if (section == "[furniture]") {
      std::istringstream fields(line);
      Placement placement;
      if (!(fields >> placement.x >> placement.y >> placement.model >>
            placement.yaw_deg)) {
        if (error) *error = "bad furniture line: " + line;
        return false;
      }
      // Optional: the offsets from the cell centre and the lift, as decor has
      // them (a piece pushed against the wall behind it).
      fields >> placement.off_x >> placement.off_z >> placement.lift;
      furniture.push_back(placement);
    } else if (section == "[decor]") {
      std::istringstream fields(line);
      Placement placement;
      placement.solid = false;
      if (!(fields >> placement.x >> placement.y >> placement.model >>
            placement.yaw_deg)) {
        if (error) *error = "bad decor line: " + line;
        return false;
      }
      fields >> placement.off_x >> placement.off_z >> placement.lift;
      decor.push_back(placement);
    } else if (section == "[decals]") {
      Decal decal;
      if (!ParseDecalLine(line, &decal)) {
        if (error) *error = "bad decal line: " + line;
        return false;
      }
      decals.push_back(std::move(decal));
    } else if (section == "[triggers]") {
      Trigger trigger;
      if (!ParseTriggerLine(line, &trigger) ||
          !trigger_ids.insert(trigger.id).second) {
        if (error) *error = "bad or duplicate trigger line: " + line;
        return false;
      }
      triggers.push_back(std::move(trigger));
    }
  }
  if (!Parse(rows, error)) return false;
  for (const auto& placement : furniture) {
    if (!InBounds(placement.x, placement.y)) {
      if (error) *error = "furniture outside the map: " + placement.model;
      return false;
    }
    // Furniture is validated after the map is known, so a bad level is rejected
    // whole.
    solid_[static_cast<size_t>(placement.y) * w_ + placement.x] = 1;
  }
  for (const auto& placement : decor) {
    if (!InBounds(placement.x, placement.y)) {
      if (error) *error = "decor outside the map: " + placement.model;
      return false;
    }
  }
  for (const auto& decal : decals) {
    if (!InBounds(decal.x, decal.y)) {
      if (error) *error = "decal outside the map: " + decal.texture;
      return false;
    }
  }
  for (const auto& trigger : triggers) {
    if (trigger.shape == TriggerShape::kCell &&
        !InBounds(trigger.cell_x, trigger.cell_z)) {
      if (error) *error = "trigger outside the map: " + trigger.id;
      return false;
    }
  }
  furniture_ = std::move(furniture);
  furniture_.insert(furniture_.end(), decor.begin(), decor.end());
  for (const auto& decal : decals) AddDecal(decal);
  triggers_ = std::move(triggers);
  return true;
}

char Level::At(const int x, const int y) const {
  return InBounds(x, y) ? cells_[static_cast<size_t>(y) * w_ + x] : '#';
}

void Level::Set(const int x, const int y, const char glyph) {
  if (!InBounds(x, y)) return;
  char& cell = cells_[static_cast<size_t>(y) * w_ + x];
  if (cell == glyph) return;
  cell = glyph;
  revision_ = NextRevision();
}

bool Level::Passable(const int x, const int y) const {
  if (!InBounds(x, y)) return false;
  // Both the glyph and any furniture decide passability.
  return !tiles_->Solid(At(x, y)) &&
         solid_[static_cast<size_t>(y) * w_ + x] == 0;
}

unsigned Level::ThinArms(const int x, const int y) const {
  const auto joins = [&](const int nx, const int ny) {
    if (!InBounds(nx, ny)) return false;
    const char c = At(nx, ny);
    return tiles_->Solid(c) || tiles_->IsDoor(c);
  };
  return (joins(x, y - 1) ? 1u : 0u) | (joins(x, y + 1) ? 2u : 0u) |
         (joins(x - 1, y) ? 4u : 0u) | (joins(x + 1, y) ? 8u : 0u);
}

std::vector<Level::Rect> Level::ThinRects(const int x, const int y) const {
  std::vector<Rect> out;
  if (!InBounds(x, y)) return out;
  const char c = At(x, y);
  if (!tiles_->Solid(c) || !tiles_->Thin(c)) return out;
  const double a = 0.5 - kThinWallThickness / 2,
               b = 0.5 + kThinWallThickness / 2;
  const double fx = x, fz = y;
  const unsigned arms = ThinArms(x, y);
  out.push_back({fx + a, fz + a, fx + b, fz + b});
  if (arms & 1u) out.push_back({fx + a, fz, fx + b, fz + a});
  if (arms & 2u) out.push_back({fx + a, fz + b, fx + b, fz + 1});
  if (arms & 4u) out.push_back({fx, fz + a, fx + a, fz + b});
  if (arms & 8u) out.push_back({fx + b, fz + a, fx + 1, fz + b});
  return out;
}

bool Level::BoxBlocked(const double min_x, const double min_z,
                       const double max_x, const double max_z) const {
  const int x0 = static_cast<int>(std::floor(min_x));
  const int x1 = static_cast<int>(std::floor(max_x));
  const int z0 = static_cast<int>(std::floor(min_z));
  const int z1 = static_cast<int>(std::floor(max_z));
  for (int cy = z0; cy <= z1; ++cy) {
    for (int cx = x0; cx <= x1; ++cx) {
      if (Passable(cx, cy)) continue;
      // Thin walls are the only blocked cells with sub-cell shape; anything
      // else blocks the whole cell.
      if (!InBounds(cx, cy) || solid_[static_cast<size_t>(cy) * w_ + cx] != 0 ||
          !tiles_->Thin(At(cx, cy))) {
        return true;
      }
      for (const Rect& r : ThinRects(cx, cy)) {
        if (min_x < r.x1 && max_x > r.x0 && min_z < r.z1 && max_z > r.z0) {
          return true;
        }
      }
    }
  }
  return false;
}

bool Level::PointBlocked(const double x, const double z) const {
  return BoxBlocked(x, z, x, z);
}

bool Level::AddFurniture(const Placement& placement) {
  if (!InBounds(placement.x, placement.y) ||
      (placement.solid && !Passable(placement.x, placement.y))) {
    return false;
  }
  furniture_.push_back(placement);
  if (placement.solid) {
    solid_[static_cast<size_t>(placement.y) * w_ + placement.x] = 1;
  }
  revision_ = NextRevision();
  return true;
}

bool Level::AddDecal(const Decal& decal) {
  if (!InBounds(decal.x, decal.y) || !ValidDecal(decal)) return false;
  // Copy first because decal may refer to an element evicted below.
  Decal copy = decal;
  if (decals_.size() == kMaxDecals) decals_.erase(decals_.begin());
  decals_.push_back(std::move(copy));
  revision_ = NextRevision();
  return true;
}

bool Level::AddTrigger(const Trigger& trigger) {
  if (SerializeTriggerLine(trigger).empty() ||
      (trigger.shape == TriggerShape::kCell &&
       !InBounds(trigger.cell_x, trigger.cell_z)) ||
      std::ranges::any_of(triggers_,
                          [&](const Trigger& t) { return t.id == trigger.id; }))
    return false;
  triggers_.push_back(trigger);
  revision_ = NextRevision();
  return true;
}

bool Level::RemoveTrigger(const std::string& id) {
  const auto it = std::ranges::find_if(
      triggers_, [&](const Trigger& t) { return t.id == id; });
  if (it == triggers_.end()) return false;
  triggers_.erase(it);
  revision_ = NextRevision();
  return true;
}

bool Level::RemovePlacement(const std::string& id) {
  if (id.empty()) return false;
  const auto it = std::ranges::find_if(
      furniture_,
      [&](const Placement& placement) { return placement.id == id; });
  if (it == furniture_.end()) return false;
  const int x = it->x, y = it->y;
  furniture_.erase(it);
  // Another solid piece may still share the cell.
  solid_[static_cast<size_t>(y) * w_ + x] = FurnitureAt(x, y) ? 1 : 0;
  revision_ = NextRevision();
  return true;
}

const Placement* Level::FurnitureAt(const int x, const int y) const {
  for (const auto& placement : furniture_) {
    if (placement.solid && placement.x == x && placement.y == y) {
      return &placement;
    }
  }
  return nullptr;
}

bool Level::Find(const char glyph, int& x, int& y) const {
  for (int row = 0; row < h_; ++row) {
    for (int col = 0; col < w_; ++col) {
      if (At(col, row) == glyph) {
        x = col;
        y = row;
        return true;
      }
    }
  }
  return false;
}

std::vector<std::pair<int, int>> Level::FindAll(const char glyph) const {
  std::vector<std::pair<int, int>> out;
  for (int row = 0; row < h_; ++row) {
    for (int col = 0; col < w_; ++col) {
      if (At(col, row) == glyph) out.emplace_back(col, row);
    }
  }
  return out;
}

bool Level::LineOfSight(const double x0, const double y0, const double x1,
                        const double y1) const {
  const double dx = x1 - x0;
  const double dy = y1 - y0;
  const double length = std::sqrt(dx * dx + dy * dy);
  const int steps = std::max(1, static_cast<int>(length / kSightStep));
  for (int step = 1; step < steps; ++step) {
    const double along = static_cast<double>(step) / steps;
    if (PointBlocked(x0 + dx * along, y0 + dy * along)) {
      return false;
    }
  }
  return true;
}

bool Level::LoadFile(const std::string& path, std::string* const error) {
  std::string text;
  if (!ReadFile(path, text)) {
    if (error) *error = "missing level file " + path;
    return false;
  }
  std::string err;
  if (!ParseText(text, &err)) {
    if (error) *error = path + ": " + err;
    return false;
  }
  return true;
}
}
