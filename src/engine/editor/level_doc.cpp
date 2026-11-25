#include "engine/editor/level_doc.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>

namespace mythos::editor {
namespace {
// Strips spaces, tabs and carriage returns from both ends.
std::string Trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
    --b;
  return s.substr(a, b - a);
}
}

bool LevelDoc::Set(const int x, const int y, const char glyph) {
  if (!InBounds(x, y)) return false;
  rows[y][x] = glyph;
  return true;
}

void LevelDoc::Resize(const int width, const int height, const char fill) {
  if (width < 1 || height < 1) return;
  const int old_w = Width();
  for (auto& r : rows) r.resize(static_cast<size_t>(width), fill);
  rows.resize(static_cast<size_t>(height),
              std::string(static_cast<size_t>(width), fill));
  (void)old_w;
  auto outside = [&](const Item& i) { return i.x >= width || i.y >= height; };
  std::erase_if(furniture, outside);
  std::erase_if(decor, outside);
  std::erase_if(decals, [&](const Decal& d) { return !InBounds(d.x, d.y); });
  std::erase_if(triggers, [&](const Trigger& t) {
    return t.shape == TriggerShape::kCell && !InBounds(t.cell_x, t.cell_z);
  });
}

std::string FormatNumber(const double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.4f", v);
  std::string s = buf;
  if (s.find('.') != std::string::npos) {
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
  }
  if (s == "-0") s = "0";
  return s;
}

bool ParseLevelDoc(const std::string& text, LevelDoc* const out,
                   std::string* const error) {
  LevelDoc doc;
  std::istringstream in(text);
  std::string raw, section;
  bool seen_map = false;
  std::set<std::string> trigger_ids, rule_ids;
  auto fail = [&](const std::string& what) {
    if (error) *error = what;
    return false;
  };
  while (std::getline(in, raw)) {
    const std::string line = Trim(raw);
    if (line.empty()) continue;
    if (line[0] == ';') {
      // Only comments before `[map]` are kept as the header.
      if (!seen_map) doc.header.push_back(Trim(line.substr(1)));
      continue;
    }
    if (line[0] == '[') {
      section = line;
      if (section == "[map]") seen_map = true;
      continue;
    }
    if (section == "[map]") {
      // Rows are kept untrimmed (a glyph may be a space), but a file with
      // trailing blanks on the lines is tolerated.
      if (!doc.rows.empty() && doc.rows[0].size() != raw.size() &&
          doc.rows[0].size() != Trim(raw).size())
        return fail("rows differ in length: " + line);
      doc.rows.push_back(raw.substr(0, raw.find_last_not_of('\r') + 1));
    } else if (section == "[furniture]" || section == "[decor]") {
      std::istringstream ls(line);
      Item it;
      if (!(ls >> it.x >> it.y >> it.model >> it.yaw))
        return fail("bad line: " + line);
      if (section == "[decor]") {
        if (!(ls >> it.off_x >> it.off_z >> it.lift))
          return fail("bad decor line: " + line);
        if (std::string tag; ls >> tag && tag[0] == '@')
          it.link = tag.substr(1);
        doc.decor.push_back(it);
      } else {
        ls >> it.off_x >> it.off_z >> it.lift;  // optional on furniture
        if (std::string tag; ls >> tag && tag[0] == '@')
          it.link = tag.substr(1);
        doc.furniture.push_back(it);
      }
    } else if (section == "[decals]") {
      Decal decal;
      if (!ParseDecalLine(line, &decal)) return fail("bad decal line: " + line);
      doc.decals.push_back(std::move(decal));
    } else if (section == "[triggers]") {
      Trigger trigger;
      if (!ParseTriggerLine(line, &trigger) ||
          !trigger_ids.insert(trigger.id).second)
        return fail("bad or duplicate trigger line: " + line);
      doc.triggers.push_back(std::move(trigger));
    } else if (section == "[rules]") {
      EventRule rule;
      if (!ParseEventRuleLine(line, &rule) || !rule_ids.insert(rule.id).second)
        return fail("bad or duplicate rule line: " + line);
      doc.rules.push_back(std::move(rule));
    } else if (section == "[links]") {
      std::istringstream ls(line);
      PrefabLink link;
      if (!(ls >> link.id >> link.prefab >> link.x >> link.y >> link.turns))
        return fail("bad link line: " + line);
      doc.links.push_back(link);
    } else if (section == "[link_decals]") {
      // `<index into [decals]> <link id>`: which decals a link placed.
      std::istringstream ls(line);
      size_t index = 0;
      std::string id;
      if (!(ls >> index >> id) || index >= doc.decals.size())
        return fail("bad link decal line: " + line);
      doc.decals[index].link = id;
    } else {
      return fail("line outside a section: " + line);
    }
  }
  if (doc.rows.empty()) return fail("no [map]");
  for (const auto& r : doc.rows)
    if (r.size() != doc.rows[0].size()) return fail("rows differ in length");
  for (const auto& decal : doc.decals)
    if (!doc.InBounds(decal.x, decal.y))
      return fail("decal outside the map: " + decal.texture);
  for (const auto& trigger : doc.triggers)
    if (trigger.shape == TriggerShape::kCell &&
        !doc.InBounds(trigger.cell_x, trigger.cell_z))
      return fail("trigger outside the map: " + trigger.id);
  *out = std::move(doc);
  return true;
}

std::string SerializeLevelDoc(const LevelDoc& doc) {
  std::set<std::string> trigger_ids;
  for (const auto& trigger : doc.triggers) {
    if (SerializeTriggerLine(trigger).empty() ||
        !trigger_ids.insert(trigger.id).second ||
        (trigger.shape == TriggerShape::kCell &&
         !doc.InBounds(trigger.cell_x, trigger.cell_z)))
      return {};
  }
  std::set<std::string> rule_ids;
  for (const auto& rule : doc.rules)
    if (SerializeEventRuleLine(rule).empty() ||
        !rule_ids.insert(rule.id).second)
      return {};
  std::string s;
  bool stamped = false;
  for (const auto& h : doc.header) {
    if (h.find("map_editor") != std::string::npos) stamped = true;
    s += "; " + h + "\n";
  }
  if (!stamped) s = "; Saved by the House of Years editor (map_editor).\n" + s;
  s += "[map]\n";
  for (const auto& r : doc.rows) s += r + "\n";
  s += "[furniture]\n";
  for (const auto& i : doc.furniture) {
    s += std::to_string(i.x) + " " + std::to_string(i.y) + " " + i.model + " " +
         FormatNumber(i.yaw);
    // Offsets when pushed against a wall, or always before a link tag.
    if (i.off_x != 0 || i.off_z != 0 || i.lift != 0 || !i.link.empty())
      s += " " + FormatNumber(i.off_x) + " " + FormatNumber(i.off_z) + " " +
           FormatNumber(i.lift);
    if (!i.link.empty()) s += " @" + i.link;
    s += '\n';
  }
  s += "[decor]\n";
  for (const auto& i : doc.decor)
    s += std::to_string(i.x) + " " + std::to_string(i.y) + " " + i.model + " " +
         FormatNumber(i.yaw) + " " + FormatNumber(i.off_x) + " " +
         FormatNumber(i.off_z) + " " + FormatNumber(i.lift) +
         (i.link.empty() ? "" : " @" + i.link) + "\n";
  if (!doc.decals.empty()) {
    s += "[decals]\n";
    for (const auto& decal : doc.decals) s += SerializeDecalLine(decal) + "\n";
  }
  {
    std::string tagged;
    for (size_t i = 0; i < doc.decals.size(); ++i) {
      if (!doc.decals[i].link.empty())
        tagged += std::to_string(i) + " " + doc.decals[i].link + "\n";
    }
    if (!tagged.empty()) s += "[link_decals]\n" + tagged;
  }
  if (!doc.links.empty()) {
    s += "[links]\n";
    for (const PrefabLink& l : doc.links)
      s += l.id + " " + l.prefab + " " + std::to_string(l.x) + " " +
           std::to_string(l.y) + " " + std::to_string(l.turns) + "\n";
  }
  if (!doc.triggers.empty()) {
    s += "[triggers]\n";
    for (const auto& trigger : doc.triggers)
      s += SerializeTriggerLine(trigger) + "\n";
  }
  if (!doc.rules.empty()) {
    s += "[rules]\n";
    for (const auto& rule : doc.rules) s += SerializeEventRuleLine(rule) + "\n";
  }
  return s;
}

bool RenameTrigger(LevelDoc& doc, const std::string& from,
                   const std::string& to, int* const rules_updated) {
  if (rules_updated != nullptr) *rules_updated = 0;
  const auto has = [&](const std::string& id) {
    return std::ranges::any_of(doc.triggers,
                               [&](const Trigger& t) { return t.id == id; });
  };
  if (to.empty() || !has(from)) return false;
  if (from == to) return true;
  if (has(to)) return false;
  for (Trigger& t : doc.triggers) {
    if (t.id == from) t.id = to;
  }
  int n = 0;
  for (EventRule& r : doc.rules) {
    if (r.event.kind == EventKind::kTrigger && r.event.id == from) {
      r.event.id = to;
      ++n;
    }
  }
  if (rules_updated != nullptr) *rules_updated = n;
  return true;
}

int RemoveTrigger(LevelDoc& doc, const std::string& id) {
  const auto it = std::ranges::find_if(
      doc.triggers, [&](const Trigger& t) { return t.id == id; });
  if (it == doc.triggers.end()) return -1;
  doc.triggers.erase(it);
  const size_t before = doc.rules.size();
  std::erase_if(doc.rules, [&](const EventRule& r) {
    return r.event.kind == EventKind::kTrigger && r.event.id == id;
  });
  return static_cast<int>(before - doc.rules.size());
}

std::string NewRuleId(const LevelDoc& doc) {
  for (size_t n = 1;; ++n) {
    const std::string id = "rule_" + std::to_string(n);
    if (std::ranges::none_of(doc.rules,
                             [&](const EventRule& r) { return r.id == id; })) {
      return id;
    }
  }
}
}
