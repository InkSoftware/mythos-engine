#include "engine/editor/level_doc.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace mythos::editor {

namespace {

std::string Trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
    --b;
  return s.substr(a, b - a);
}

}  // namespace

bool LevelDoc::Set(int x, int y, char glyph) {
  if (!InBounds(x, y)) return false;
  rows[y][x] = glyph;
  return true;
}

void LevelDoc::Resize(int width, int height, char fill) {
  if (width < 1 || height < 1) return;
  const int old_w = Width();
  for (auto& r : rows) r.resize(static_cast<size_t>(width), fill);
  rows.resize(static_cast<size_t>(height),
              std::string(static_cast<size_t>(width), fill));
  (void)old_w;
  auto outside = [&](const Item& i) { return i.x >= width || i.y >= height; };
  std::erase_if(furniture, outside);
  std::erase_if(decor, outside);
}

std::string FormatNumber(double v) {
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

bool ParseLevelDoc(const std::string& text, LevelDoc* out, std::string* error) {
  LevelDoc doc;
  std::istringstream in(text);
  std::string raw, section;
  bool seen_map = false;
  auto fail = [&](const std::string& what) {
    if (error) *error = what;
    return false;
  };
  while (std::getline(in, raw)) {
    const std::string line = Trim(raw);
    if (line.empty()) continue;
    if (line[0] == ';') {
      if (!seen_map) doc.header.push_back(Trim(line.substr(1)));
      continue;
    }
    if (line[0] == '[') {
      section = line;
      if (section == "[map]") seen_map = true;
      continue;
    }
    if (section == "[map]") {
      if (!doc.rows.empty() && doc.rows[0].size() != raw.size() &&
          doc.rows[0].size() != Trim(raw).size())
        return fail("rows differ in length: " + line);
      doc.rows.push_back(raw.substr(0, raw.find_last_not_of("\r") + 1));
    } else if (section == "[furniture]" || section == "[decor]") {
      std::istringstream ls(line);
      Item it;
      if (!(ls >> it.x >> it.y >> it.model >> it.yaw))
        return fail("bad line: " + line);
      if (section == "[decor]") {
        if (!(ls >> it.off_x >> it.off_z >> it.lift))
          return fail("bad decor line: " + line);
        doc.decor.push_back(it);
      } else {
        doc.furniture.push_back(it);
      }
    } else {
      return fail("line outside a section: " + line);
    }
  }
  if (doc.rows.empty()) return fail("no [map]");
  for (const auto& r : doc.rows)
    if (r.size() != doc.rows[0].size()) return fail("rows differ in length");
  *out = std::move(doc);
  return true;
}

std::string SerializeLevelDoc(const LevelDoc& doc) {
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
  for (const auto& i : doc.furniture)
    s += std::to_string(i.x) + " " + std::to_string(i.y) + " " + i.model + " " +
         FormatNumber(i.yaw) + "\n";
  s += "[decor]\n";
  for (const auto& i : doc.decor)
    s += std::to_string(i.x) + " " + std::to_string(i.y) + " " + i.model + " " +
         FormatNumber(i.yaw) + " " + FormatNumber(i.off_x) + " " +
         FormatNumber(i.off_z) + " " + FormatNumber(i.lift) + "\n";
  return s;
}

}  // namespace mythos::editor
