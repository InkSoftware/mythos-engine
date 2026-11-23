#ifndef MYTHOS_ENGINE_WORLD_NAV_H_
#define MYTHOS_ENGINE_WORLD_NAV_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <vector>

#include "engine/world/level.h"

namespace mythos {
// Grid coordinates: x is the column, y is the row (world z).
struct NavCell {
  int x = 0;
  int y = 0;

  bool operator==(const NavCell&) const = default;
};

struct NavOptions {
  // Exclude all door tiles, including open ones. Otherwise the tile's current
  // solidity decides; this does not open doors or predict their animation.
  bool avoid_doors = false;
  // Additionally check the bounding square of a body at each center and its
  // swept square along each step with Level::BoxBlocked.
  bool check_occupancy = false;
  double radius = 0.2;
};

namespace nav_internal {
class Grid {
 public:
  Grid(const Level& level, const NavOptions& options)
      : level_(level), options_(options) {}

  bool Valid() const {
    if (level_.Width() <= 0 || level_.Height() <= 0) return false;
    if (options_.check_occupancy &&
        (!std::isfinite(options_.radius) || options_.radius < 0))
      return false;
    return static_cast<std::size_t>(level_.Width()) <=
           std::numeric_limits<std::size_t>::max() /
               static_cast<std::size_t>(level_.Height());
  }

  std::size_t Size() const {
    return static_cast<std::size_t>(level_.Width()) *
           static_cast<std::size_t>(level_.Height());
  }

  std::size_t Index(const NavCell cell) const {
    return static_cast<std::size_t>(cell.y) *
               static_cast<std::size_t>(level_.Width()) +
           static_cast<std::size_t>(cell.x);
  }

  NavCell Cell(const std::size_t index) const {
    const auto width = static_cast<std::size_t>(level_.Width());
    return {static_cast<int>(index % width), static_cast<int>(index / width)};
  }

  bool Clear(const NavCell from, const NavCell to) const {
    if (!options_.check_occupancy) return true;
    const double r = options_.radius;
    const double min_x = std::min(from.x, to.x) + 0.5 - r;
    const double min_y = std::min(from.y, to.y) + 0.5 - r;
    const double max_x = std::max(from.x, to.x) + 0.5 + r;
    const double max_y = std::max(from.y, to.y) + 0.5 + r;
    // Keep BoxBlocked's floor-to-int conversions in bounds even for enormous
    // radii. Its upper edge at the map boundary includes the outside cell.
    if (min_x < 0 || min_y < 0 || max_x >= level_.Width() ||
        max_y >= level_.Height())
      return false;
    return !level_.BoxBlocked(min_x, min_y, max_x, max_y);
  }

  bool Passable(const NavCell cell) const {
    return level_.Passable(cell.x, cell.y) &&
           (!options_.avoid_doors ||
            !level_.Tiles().IsDoor(level_.At(cell.x, cell.y))) &&
           Clear(cell, cell);
  }

 private:
  const Level& level_;
  const NavOptions& options_;
};

// East, west, south, north, in that order. Coordinates supplied here are
// already in bounds, so adding a unit cannot overflow int.
inline std::array<NavCell, 4> Neighbors(const NavCell cell) {
  return {NavCell{cell.x + 1, cell.y}, NavCell{cell.x - 1, cell.y},
          NavCell{cell.x, cell.y + 1}, NavCell{cell.x, cell.y - 1}};
}

inline uint64_t Distance(const NavCell a, const NavCell b) {
  const auto dx = static_cast<int64_t>(a.x) - b.x;
  const auto dy = static_cast<int64_t>(a.y) - b.y;
  return static_cast<uint64_t>(dx < 0 ? -dx : dx) +
         static_cast<uint64_t>(dy < 0 ? -dy : dy);
}
}  // namespace nav_internal

// A* on in-bounds Level::Passable cells, with unit-cost cardinal steps. Returns
// a shortest path including start and goal, or empty for an invalid/blocked
// endpoint, invalid options, or no route. Equal endpoints return one cell if
// passable. Equal-priority choices are expanded in discovery order; neighbors
// are discovered east, west, south, north. No smoothing or diagonal steps.
//
// If check_occupancy is set, radius must be finite and nonnegative. Every
// center and the entire axis-aligned swept bounding square of every step must
// pass Level::BoxBlocked, including its boundary and thin-wall semantics. This
// is conservative for a round body, and still excludes whole solid thin-wall
// cells via Passable. Radius is ignored when the check is off. avoid_doors
// excludes door cells, not every square that overlaps one. The caller must not
// mutate the level during a query and must re-query after collision changes.
[[nodiscard]] inline std::vector<NavCell> FindPath(
    const Level& level, const NavCell start, const NavCell goal,
    const NavOptions& options = {}) {
  const nav_internal::Grid grid(level, options);
  if (!grid.Valid() || !grid.Passable(start) || !grid.Passable(goal)) return {};

  struct Entry {
    uint64_t score;
    uint64_t distance;
    uint64_t order;
    std::size_t index;
  };
  struct Later {
    bool operator()(const Entry& a, const Entry& b) const {
      return a.score != b.score ? a.score > b.score : a.order > b.order;
    }
  };
  const auto unset = std::numeric_limits<std::size_t>::max();
  const auto infinity = std::numeric_limits<uint64_t>::max();
  std::vector<std::size_t> parent(grid.Size(), unset);
  std::vector<uint64_t> distance(grid.Size(), infinity);
  std::priority_queue<Entry, std::vector<Entry>, Later> open;
  const std::size_t start_index = grid.Index(start),
                    goal_index = grid.Index(goal);
  uint64_t order = 0;
  distance[start_index] = 0;
  open.push({nav_internal::Distance(start, goal), 0, order++, start_index});

  while (!open.empty()) {
    const Entry current = open.top();
    open.pop();
    if (current.distance != distance[current.index]) continue;
    if (current.index == goal_index) {
      std::vector<NavCell> path;
      for (std::size_t i = goal_index; i != unset; i = parent[i])
        path.push_back(grid.Cell(i));
      std::reverse(path.begin(), path.end());
      return path;
    }
    const NavCell cell = grid.Cell(current.index);
    for (const NavCell next : nav_internal::Neighbors(cell)) {
      if (!grid.Passable(next) || !grid.Clear(cell, next)) continue;
      const auto index = grid.Index(next);
      const uint64_t next_distance = current.distance + 1;
      if (next_distance >= distance[index]) continue;
      distance[index] = next_distance;
      parent[index] = current.index;
      open.push({next_distance + nav_internal::Distance(next, goal),
                 next_distance, order++, index});
    }
  }
  return {};
}

// Floods the same graph as FindPath. Returns each reachable cell once, start
// included, in breadth-first discovery order (east, west, south, north); empty
// for an invalid/blocked start or invalid options. There is no distance cap.
[[nodiscard]] inline std::vector<NavCell> ReachableCells(
    const Level& level, const NavCell start, const NavOptions& options = {}) {
  const nav_internal::Grid grid(level, options);
  if (!grid.Valid() || !grid.Passable(start)) return {};

  std::vector<bool> seen(grid.Size(), false);
  std::vector<NavCell> cells{start};
  seen[grid.Index(start)] = true;
  for (std::size_t i = 0; i < cells.size(); ++i) {
    const NavCell cell = cells[i];
    for (const NavCell next : nav_internal::Neighbors(cell)) {
      if (!grid.Passable(next)) continue;
      const auto index = grid.Index(next);
      if (seen[index] || !grid.Clear(cell, next)) continue;
      seen[index] = true;
      cells.push_back(next);
    }
  }
  return cells;
}
}  // namespace mythos

#endif  // MYTHOS_ENGINE_WORLD_NAV_H_