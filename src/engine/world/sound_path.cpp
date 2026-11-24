#include "engine/world/sound_path.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace mythos {
SoundBlockers CountSoundBlockers(const Level& level, const Vec2& from,
                                 const Vec2& to) {
  SoundBlockers out;
  const auto cell = [](const double v) {
    return static_cast<int>(std::floor(v));
  };
  const std::pair<int, int> start{cell(from.x), cell(from.y)};
  const std::pair<int, int> end{cell(to.x), cell(to.y)};
  const double length = std::hypot(to.x - from.x, to.y - from.y);
  // A step of a fifth of a cell cannot skip a cell the line crosses.
  const int steps = std::max(1, static_cast<int>(std::ceil(length * 5)));
  std::set<std::pair<int, int>> seen;
  for (int i = 0; i <= steps; ++i) {
    const double t = static_cast<double>(i) / steps;
    const std::pair<int, int> c{cell(from.x + (to.x - from.x) * t),
                                cell(from.y + (to.y - from.y) * t)};
    if (c == start || c == end || !seen.insert(c).second) continue;
    const char glyph = level.At(c.first, c.second);
    const TileSet& tiles = level.Tiles();
    if (tiles.IsClosedDoor(glyph)) {
      ++out.closed_doors;
    } else if (tiles.Solid(glyph) || tiles.Thin(glyph)) {
      ++out.walls;
    }
  }
  return out;
}
}
