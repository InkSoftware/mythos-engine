#include "engine/editor/prefab.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <sstream>

namespace mythos::editor {
namespace {
std::string Trim(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

// One clockwise quarter turn of an item in a footprint of `height` rows.
Item Turned(const Item& i, const int height) {
  Item t = i;
  t.x = height - 1 - i.y;
  t.y = i.x;
  t.off_x = -i.off_z;
  t.off_z = i.off_x;
  t.yaw = std::fmod(i.yaw - 90.0 + 720.0, 360.0);
  return t;
}

// The face a wall decal looks along after a clockwise quarter turn on the map
// (north becomes east, east south, south west, west north); a floor or ceiling
// keeps its face.
DecalFace TurnedFace(const DecalFace f) {
  switch (f) {
    case DecalFace::kNorth:
      return DecalFace::kEast;
    case DecalFace::kEast:
      return DecalFace::kSouth;
    case DecalFace::kSouth:
      return DecalFace::kWest;
    case DecalFace::kWest:
      return DecalFace::kNorth;
    case DecalFace::kThinNorth:
      return DecalFace::kThinEast;
    case DecalFace::kThinEast:
      return DecalFace::kThinSouth;
    case DecalFace::kThinSouth:
      return DecalFace::kThinWest;
    case DecalFace::kThinWest:
      return DecalFace::kThinNorth;
    case DecalFace::kGlassNorth:
      return DecalFace::kGlassEast;
    case DecalFace::kGlassEast:
      return DecalFace::kGlassSouth;
    case DecalFace::kGlassSouth:
      return DecalFace::kGlassWest;
    case DecalFace::kGlassWest:
      return DecalFace::kGlassNorth;
    default:
      return f;
  }
}

bool Horizontal(const DecalFace f) {
  return f == DecalFace::kFloor || f == DecalFace::kCeiling;
}

Decal TurnedDecal(const Decal& d, const int height) {
  Decal t = d;
  t.x = height - 1 - d.y;
  t.y = d.x;
  t.face = TurnedFace(d.face);
  if (Horizontal(d.face)) {
    // u runs +x and v +z on a floor or ceiling: turn the offset with the cell
    // and the decal itself a quarter in its plane.
    t.offset_u = -d.offset_v;
    t.offset_v = d.offset_u;
    t.rotation = std::fmod(d.rotation + 90.0, 360.0);
  }
  return t;
}

// A clockwise quarter turn about the footprint of `height` rows: a cell
// trigger's cell as an item's, a box's corners as (x, z) -> (height - z, x).
Trigger TurnedTrigger(const Trigger& in, const int height) {
  Trigger t = in;
  if (in.shape == TriggerShape::kCell) {
    t.cell_x = height - 1 - in.cell_z;
    t.cell_z = in.cell_x;
  } else {
    t.min.x = height - in.max.z;
    t.max.x = height - in.min.z;
    t.min.z = in.min.x;
    t.max.z = in.max.x;
  }
  return t;
}

void AppendItems(std::string& s, const std::vector<Item>& items,
                 const bool always_offsets) {
  for (const Item& i : items) {
    s += std::to_string(i.x) + " " + std::to_string(i.y) + " " + i.model + " " +
         FormatNumber(i.yaw);
    if (always_offsets || i.off_x != 0 || i.off_z != 0 || i.lift != 0) {
      s += " " + FormatNumber(i.off_x) + " " + FormatNumber(i.off_z) + " " +
           FormatNumber(i.lift);
    }
    s += '\n';
  }
}
}

bool ParsePrefab(const std::string& text, Prefab* const out,
                 std::string* const error) {
  const auto fail = [&](const std::string& why) {
    if (error != nullptr) *error = why;
    return false;
  };
  Prefab p;
  bool sized = false;
  std::string section;
  std::istringstream in(text);
  for (std::string raw; std::getline(in, raw);) {
    const std::string line = Trim(raw);
    if (line.empty() || line[0] == ';') continue;
    if (line[0] == '[') {
      section = line;
      if (section != "[size]" && section != "[furniture]" &&
          section != "[decor]" && section != "[decals]" &&
          section != "[triggers]" && section != "[rules]") {
        return fail("unknown section: " + line);
      }
      continue;
    }
    std::istringstream ls(line);
    if (section == "[size]") {
      if (!(ls >> p.width >> p.height) || p.width < 1 || p.height < 1) {
        return fail("bad size: " + line);
      }
      sized = true;
    } else if (section == "[furniture]" || section == "[decor]") {
      Item it;
      if (!(ls >> it.x >> it.y >> it.model >> it.yaw)) {
        return fail("bad line: " + line);
      }
      if (section == "[decor]") {
        if (!(ls >> it.off_x >> it.off_z >> it.lift)) {
          return fail("bad decor line: " + line);
        }
        p.decor.push_back(it);
      } else {
        ls >> it.off_x >> it.off_z >> it.lift;  // optional on furniture
        p.furniture.push_back(it);
      }
    } else if (section == "[decals]") {
      Decal d;
      if (!ParseDecalLine(line, &d)) return fail("bad decal line: " + line);
      p.decals.push_back(std::move(d));
    } else if (section == "[triggers]") {
      Trigger t;
      if (!ParseTriggerLine(line, &t)) return fail("bad trigger line: " + line);
      p.triggers.push_back(std::move(t));
    } else if (section == "[rules]") {
      EventRule r;
      if (!ParseEventRuleLine(line, &r)) return fail("bad rule line: " + line);
      p.rules.push_back(std::move(r));
    } else {
      return fail("a line before any section: " + line);
    }
  }
  const auto extent = [&](const std::vector<Item>& items) {
    for (const Item& i : items) {
      p.width = sized ? p.width : std::max(p.width, i.x + 1);
      p.height = sized ? p.height : std::max(p.height, i.y + 1);
    }
  };
  extent(p.furniture);
  extent(p.decor);
  for (const Decal& d : p.decals) {
    p.width = sized ? p.width : std::max(p.width, d.x + 1);
    p.height = sized ? p.height : std::max(p.height, d.y + 1);
  }
  for (const Trigger& t : p.triggers) {
    if (t.shape != TriggerShape::kCell) continue;
    p.width = sized ? p.width : std::max(p.width, t.cell_x + 1);
    p.height = sized ? p.height : std::max(p.height, t.cell_z + 1);
  }
  for (const auto* list : {&p.furniture, &p.decor}) {
    for (const Item& i : *list) {
      if (i.x < 0 || i.y < 0 || i.x >= p.width || i.y >= p.height) {
        return fail("an item outside the footprint: " + i.model);
      }
    }
  }
  for (const Decal& d : p.decals) {
    if (d.x < 0 || d.y < 0 || d.x >= p.width || d.y >= p.height)
      return fail("a decal outside the footprint: " + d.texture);
  }
  for (const Trigger& t : p.triggers) {
    if (t.shape == TriggerShape::kCell &&
        (t.cell_x < 0 || t.cell_z < 0 || t.cell_x >= p.width ||
         t.cell_z >= p.height))
      return fail("a trigger outside the footprint: " + t.id);
  }
  *out = std::move(p);
  return true;
}

std::string SerializePrefab(const Prefab& prefab) {
  std::string s =
      "; A prefab: furniture and decor relative to its top-left corner.\n";
  s += "[size]\n" + std::to_string(prefab.width) + " " +
       std::to_string(prefab.height) + "\n";
  s += "[furniture]\n";
  AppendItems(s, prefab.furniture, false);
  s += "[decor]\n";
  AppendItems(s, prefab.decor, true);
  if (!prefab.decals.empty()) {
    s += "[decals]\n";
    for (const Decal& d : prefab.decals) s += SerializeDecalLine(d) + "\n";
  }
  if (!prefab.triggers.empty()) {
    s += "[triggers]\n";
    for (const Trigger& t : prefab.triggers)
      s += SerializeTriggerLine(t) + "\n";
  }
  if (!prefab.rules.empty()) {
    s += "[rules]\n";
    for (const EventRule& r : prefab.rules)
      s += SerializeEventRuleLine(r) + "\n";
  }
  return s;
}

Prefab CapturePrefab(const LevelDoc& doc, int x0, int y0, int x1, int y1,
                     const std::string& name) {
  Prefab p;
  p.name = name;
  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);
  x0 = std::max(x0, 0);
  y0 = std::max(y0, 0);
  x1 = std::min(x1, doc.Width() - 1);
  y1 = std::min(y1, doc.Height() - 1);
  if (x0 > x1 || y0 > y1) return p;
  p.width = x1 - x0 + 1;
  p.height = y1 - y0 + 1;
  const auto take = [&](const std::vector<Item>& from, std::vector<Item>& to) {
    for (Item i : from) {
      if (i.x < x0 || i.x > x1 || i.y < y0 || i.y > y1) continue;
      i.x -= x0;
      i.y -= y0;
      to.push_back(i);
    }
  };
  take(doc.furniture, p.furniture);
  take(doc.decor, p.decor);
  for (Item& i : p.furniture) i.link.clear();
  for (Item& i : p.decor) i.link.clear();
  for (Decal d : doc.decals) {
    if (d.x < x0 || d.x > x1 || d.y < y0 || d.y > y1) continue;
    d.x -= x0;
    d.y -= y0;
    d.link.clear();
    p.decals.push_back(std::move(d));
  }
  for (Trigger t : doc.triggers) {
    if (t.shape == TriggerShape::kCell) {
      if (t.cell_x < x0 || t.cell_x > x1 || t.cell_z < y0 || t.cell_z > y1)
        continue;
      t.cell_x -= x0;
      t.cell_z -= y0;
    } else {
      if (t.min.x < x0 || t.max.x > x1 + 1 || t.min.z < y0 || t.max.z > y1 + 1)
        continue;
      t.min.x -= x0;
      t.max.x -= x0;
      t.min.z -= y0;
      t.max.z -= y0;
    }
    p.triggers.push_back(std::move(t));
  }
  for (const EventRule& r : doc.rules) {
    const bool driven = r.event.kind == EventKind::kTrigger &&
                        std::ranges::any_of(p.triggers, [&](const Trigger& t) {
                          return t.id == r.event.id;
                        });
    if (driven) p.rules.push_back(r);
  }
  return p;
}

Prefab Rotated(const Prefab& prefab, const int quarter_turns) {
  Prefab p = prefab;
  const int turns = ((quarter_turns % 4) + 4) % 4;
  for (int t = 0; t < turns; ++t) {
    Prefab next = p;
    next.width = p.height;
    next.height = p.width;
    for (Item& i : next.furniture) i = Turned(i, p.height);
    for (Item& i : next.decor) i = Turned(i, p.height);
    for (Decal& d : next.decals) d = TurnedDecal(d, p.height);
    for (Trigger& t : next.triggers) t = TurnedTrigger(t, p.height);
    p = std::move(next);
  }
  return p;
}

namespace {
std::string Tagged(const std::string& link_id, const std::string& name) {
  std::string id = link_id;
  id += '/';
  id += name;
  return id;
}

std::string UniqueId(const std::string& wanted,
                     const std::function<bool(const std::string&)>& taken) {
  if (!taken(wanted)) return wanted;
  for (int n = 2;; ++n) {
    const std::string id = wanted + "_" + std::to_string(n);
    if (!taken(id)) return id;
  }
}
}

PlaceResult PlacePrefab(LevelDoc& doc, const Prefab& prefab, const int x,
                        const int y, const int quarter_turns,
                        const std::function<bool(char)>& blocked,
                        const std::string& link_id) {
  const Prefab turned = Rotated(prefab, quarter_turns);
  PlaceResult result;
  for (Item i : turned.furniture) {
    i.x += x;
    i.y += y;
    i.link = link_id;
    const bool taken = std::ranges::any_of(
        doc.furniture, [&](const Item& f) { return f.x == i.x && f.y == i.y; });
    if (!doc.InBounds(i.x, i.y) || taken ||
        (blocked && blocked(doc.At(i.x, i.y)))) {
      ++result.skipped;
      continue;
    }
    doc.furniture.push_back(i);
    ++result.furniture;
  }
  for (Item i : turned.decor) {
    i.x += x;
    i.y += y;
    i.link = link_id;
    if (!doc.InBounds(i.x, i.y)) {
      ++result.skipped;
      continue;
    }
    doc.decor.push_back(i);
    ++result.decor;
  }
  for (Decal d : turned.decals) {
    d.x += x;
    d.y += y;
    d.link = link_id;
    if (!doc.InBounds(d.x, d.y)) {
      ++result.skipped;
      continue;
    }
    doc.decals.push_back(std::move(d));
    ++result.decals;
  }
  // Triggers, then the rules driven by them under the new ids.
  std::map<std::string, std::string> renamed;
  for (Trigger t : turned.triggers) {
    if (t.shape == TriggerShape::kCell) {
      t.cell_x += x;
      t.cell_z += y;
      if (!doc.InBounds(t.cell_x, t.cell_z)) {
        ++result.skipped;
        continue;
      }
    } else {
      t.min.x += x;
      t.max.x += x;
      t.min.z += y;
      t.max.z += y;
    }
    const std::string old = t.id;
    t.id = link_id.empty()
               ? UniqueId(old,
                          [&](const std::string& id) {
                            return std::ranges::any_of(
                                doc.triggers,
                                [&](const Trigger& o) { return o.id == id; });
                          })
               : Tagged(link_id, old);
    renamed[old] = t.id;
    doc.triggers.push_back(std::move(t));
    ++result.triggers;
  }
  for (EventRule r : turned.rules) {
    if (r.event.kind == EventKind::kTrigger) {
      const auto it = renamed.find(r.event.id);
      if (it == renamed.end()) continue;  // its trigger was left out
      r.event.id = it->second;
    }
    r.id = link_id.empty() ? UniqueId(r.id,
                                      [&](const std::string& id) {
                                        return std::ranges::any_of(
                                            doc.rules, [&](const EventRule& o) {
                                              return o.id == id;
                                            });
                                      })
                           : Tagged(link_id, r.id);
    doc.rules.push_back(std::move(r));
    ++result.rules;
  }
  if (!link_id.empty()) {
    doc.links.push_back(
        {link_id, prefab.name, x, y, ((quarter_turns % 4) + 4) % 4});
  }
  return result;
}

std::string NewLinkId(const LevelDoc& doc) {
  for (int n = 1;; ++n) {
    const std::string id = "link_" + std::to_string(n);
    if (std::ranges::none_of(doc.links,
                             [&](const PrefabLink& l) { return l.id == id; })) {
      return id;
    }
  }
}

int RemoveLinked(LevelDoc& doc, const std::string& link_id) {
  const auto link = std::ranges::find_if(
      doc.links, [&](const PrefabLink& l) { return l.id == link_id; });
  if (link == doc.links.end()) return -1;
  const std::string prefix = link_id + "/";
  int removed = 0;
  const auto count = [&](const size_t erased) {
    removed += static_cast<int>(erased);
  };
  count(std::erase_if(doc.furniture,
                      [&](const Item& i) { return i.link == link_id; }));
  count(std::erase_if(doc.decor,
                      [&](const Item& i) { return i.link == link_id; }));
  count(std::erase_if(doc.decals,
                      [&](const Decal& d) { return d.link == link_id; }));
  count(std::erase_if(doc.triggers, [&](const Trigger& t) {
    return t.id.rfind(prefix, 0) == 0;
  }));
  count(std::erase_if(doc.rules, [&](const EventRule& r) {
    return r.id.rfind(prefix, 0) == 0;
  }));
  doc.links.erase(doc.links.begin() + (link - doc.links.begin()));
  return removed;
}

bool UpdateLinked(LevelDoc& doc, const std::string& link_id,
                  const Prefab& prefab,
                  const std::function<bool(char)>& blocked,
                  PlaceResult* const result) {
  const auto it = std::ranges::find_if(
      doc.links, [&](const PrefabLink& l) { return l.id == link_id; });
  if (it == doc.links.end()) return false;
  // A copy: RemoveLinked erases the record.
  const PrefabLink link =
      *it;  // NOLINT(performance-unnecessary-copy-initialization)
  RemoveLinked(doc, link_id);
  Prefab named = prefab;
  if (named.name.empty()) named.name = link.prefab;
  const PlaceResult r =
      PlacePrefab(doc, named, link.x, link.y, link.turns, blocked, link_id);
  if (result != nullptr) *result = r;
  return true;
}

bool Unlink(LevelDoc& doc, const std::string& link_id) {
  const auto it = std::ranges::find_if(
      doc.links, [&](const PrefabLink& l) { return l.id == link_id; });
  if (it == doc.links.end()) return false;
  for (Item& i : doc.furniture)
    if (i.link == link_id) i.link.clear();
  for (Item& i : doc.decor)
    if (i.link == link_id) i.link.clear();
  for (Decal& d : doc.decals)
    if (d.link == link_id) d.link.clear();
  doc.links.erase(it);
  return true;
}

std::string SafePrefabName(const std::string& name) {
  std::string out;
  for (const char c : name) {
    const bool ok = std::isalnum(static_cast<unsigned char>(c)) != 0 ||
                    c == '_' || c == '-';
    out += ok ? c : '_';
  }
  return out.empty() ? "prefab" : out;
}
}
