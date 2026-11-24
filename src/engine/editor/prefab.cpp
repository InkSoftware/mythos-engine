#include "engine/editor/prefab.h"

#include <algorithm>
#include <cctype>
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
          section != "[decor]") {
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
  for (const auto* list : {&p.furniture, &p.decor}) {
    for (const Item& i : *list) {
      if (i.x < 0 || i.y < 0 || i.x >= p.width || i.y >= p.height) {
        return fail("an item outside the footprint: " + i.model);
      }
    }
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
    p = std::move(next);
  }
  return p;
}

PlaceResult PlacePrefab(LevelDoc& doc, const Prefab& prefab, const int x,
                        const int y, const int quarter_turns,
                        const std::function<bool(char)>& blocked) {
  const Prefab turned = Rotated(prefab, quarter_turns);
  PlaceResult result;
  for (Item i : turned.furniture) {
    i.x += x;
    i.y += y;
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
    if (!doc.InBounds(i.x, i.y)) {
      ++result.skipped;
      continue;
    }
    doc.decor.push_back(i);
    ++result.decor;
  }
  return result;
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
