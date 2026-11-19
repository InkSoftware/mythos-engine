#include "engine/world/room_doors.h"

#include <algorithm>
#include <set>
#include <utility>

namespace mythos {
namespace {

// A door lets people through (and shows as open) once it is more than half
// open.
constexpr double kPassableOpen = 0.5;
// regionSize stops counting at this many cells (also what a blocked start cell
// reports).
constexpr int kRegionCap = 400;
// Depth stops counting at this many cells.
constexpr int kDepthCap = 8;

}  // namespace

void RoomDoors::Scan(const Level& level) {
  doors_.clear();
  const TileSet& tiles = level.Tiles();
  for (int y = 0; y < level.Height(); ++y) {
    for (int x = 0; x < level.Width(); ++x) {
      const char glyph = level.At(x, y);
      if (glyph != 'N' && glyph != 'n') continue;
      Door door;
      door.x = x;
      door.y = y;
      door.along_x =
          tiles.Solid(level.At(x, y - 1)) && tiles.Solid(level.At(x, y + 1));
      door.open = glyph == 'n' ? 1.0 : 0.0;
      door.opening = glyph == 'n';
      // It swings into the room. A room is deeper than a corridor, however
      // narrow the corridor: it goes into the side that runs further straight
      // on from the door (counted over bare floor, not furniture). If the two
      // are as deep, it goes into the smaller of the two spaces the door
      // separates.
      const int side_x = door.along_x ? 1 : 0, side_y = door.along_x ? 0 : 1;
      const int ahead = Depth(level, x, y, side_x, side_y);
      const int behind = Depth(level, x, y, -side_x, -side_y);
      if (ahead != behind) {
        door.swing = ahead > behind ? 1 : -1;
      } else {
        door.swing = RegionSize(level, x + side_x, y + side_y) <=
                             RegionSize(level, x - side_x, y - side_y)
                         ? 1
                         : -1;
      }
      doors_.push_back(door);
    }
  }
}

int RoomDoors::Use(int x, int y) {
  for (auto& door : doors_) {
    if (door.x != x || door.y != y) continue;
    door.opening = !door.opening;
    return door.opening ? 1 : -1;
  }
  return 0;
}

int RoomDoors::Update(Level& level, double dt, const Vec2& body,
                      double body_radius, double open_seconds,
                      double close_seconds) {
  // A door stops blocking (glyph 'n') once past half open, and blocks again
  // once it falls below half.
  int closed = 0;
  for (auto& door : doors_) {
    if (door.opening) {
      if (door.open >= 1.0) continue;
      door.open = std::min(1.0, door.open + dt / open_seconds);
      if (door.open > kPassableOpen && level.At(door.x, door.y) == 'N') {
        level.Set(door.x, door.y, 'n');
      }
    } else if (door.open > 0) {
      double next = std::max(0.0, door.open - dt / close_seconds);
      if (next < kPassableOpen && level.At(door.x, door.y) == 'n') {
        if (Overlaps(body, body_radius, door.x, door.y)) {
          next = kPassableOpen;  // wait, ajar, until they step out
        } else {
          level.Set(door.x, door.y, 'N');
        }
      }
      door.open = next;
      if (door.open <= 0) ++closed;
    }
  }
  return closed;
}

void RoomDoors::AppendStates(std::vector<DoorState>& out) const {
  for (const auto& door : doors_) {
    out.push_back({.x = door.x,
                   .y = door.y,
                   .passage_along_x = door.along_x,
                   .open = door.open,
                   .swing = door.swing});
  }
}

int RoomDoors::Depth(const Level& level, int x, int y, int step_x, int step_y) {
  const TileSet& tiles = level.Tiles();
  int depth = 0;
  for (int i = 1; i <= kDepthCap; ++i) {
    const char glyph = level.At(x + step_x * i, y + step_y * i);
    if (tiles.Solid(glyph) || tiles.IsDoor(glyph)) break;
    ++depth;
  }
  return depth;
}

int RoomDoors::RegionSize(const Level& level, int x, int y) {
  if (!level.Passable(x, y)) return kRegionCap;
  // Flood fill that stops at doors; cells are keyed row * 1000 + column (levels
  // are narrower than 1000).
  std::set<int> seen{y * 1000 + x};
  std::vector<std::pair<int, int>> todo{{x, y}};
  const TileSet& tiles = level.Tiles();
  while (!todo.empty() && static_cast<int>(seen.size()) < kRegionCap) {
    const auto [cell_x, cell_y] = todo.back();
    todo.pop_back();
    const int step_x[4] = {1, -1, 0, 0}, step_y[4] = {0, 0, 1, -1};
    for (int dir = 0; dir < 4; ++dir) {
      const int next_x = cell_x + step_x[dir], next_y = cell_y + step_y[dir];
      if (!level.Passable(next_x, next_y) ||
          tiles.IsDoor(level.At(next_x, next_y))) {
        continue;
      }
      if (seen.insert(next_y * 1000 + next_x).second) {
        todo.push_back({next_x, next_y});
      }
    }
  }
  return static_cast<int>(seen.size());
}

bool RoomDoors::Overlaps(const Vec2& pos, double radius, int x, int y) {
  // Distance from the circle's centre to the nearest point of the door's cell.
  const double nearest_x = Clampd(pos.x, x, x + 1.0),
               nearest_y = Clampd(pos.y, y, y + 1.0);
  const double dx = pos.x - nearest_x, dy = pos.y - nearest_y;
  return dx * dx + dy * dy < radius * radius;
}

}  // namespace mythos
