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
      std::ranges::reverse(path);
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

// Pulls a cell path taut: waypoints (cell centres) from the start to the end
// that skip every cell a straight walk can cut across, tested with
// Level::BoxBlocked for a body of `radius` (the same collision the player has,
// so the walk is not blocked by thin walls or furniture). The first and last
// points are the first and last cell centres. An empty path gives an empty
// result.
[[nodiscard]] inline std::vector<Vec2> SmoothPath(
    const Level& level, const std::vector<NavCell>& path,
    const double radius = 0.2) {
  std::vector<Vec2> points;
  points.reserve(path.size());
  for (const NavCell& c : path) points.push_back({c.x + 0.5, c.y + 0.5});
  if (points.size() < 3) return points;
  const auto clear = [&](const Vec2& a, const Vec2& b) {
    const double length = std::hypot(b.x - a.x, b.y - a.y);
    const int steps = std::max(1, static_cast<int>(std::ceil(length / 0.1)));
    for (int i = 0; i <= steps; ++i) {
      const double t = static_cast<double>(i) / steps;
      const double x = a.x + (b.x - a.x) * t, y = a.y + (b.y - a.y) * t;
      if (level.BoxBlocked(x - radius, y - radius, x + radius, y + radius)) {
        return false;
      }
    }
    return true;
  };
  std::vector<Vec2> out{points.front()};
  size_t anchor = 0;
  while (anchor + 1 < points.size()) {
    // The farthest point still reachable in a straight line from the anchor.
    size_t reach = anchor + 1;
    for (size_t k = points.size() - 1; k > anchor + 1; --k) {
      if (clear(points[anchor], points[k])) {
        reach = k;
        break;
      }
    }
    out.push_back(points[reach]);
    anchor = reach;
  }
  return out;
}

// Something to steer around: a mover (another agent, a rolling prop) with its
// position, velocity (cells per second) and radius.
struct Obstacle {
  Vec2 pos;
  Vec2 vel;
  double radius = 0.3;
};

// Local avoidance: the unit direction an agent at `pos` with `radius` should
// walk to go `desired` (a direction; need not be unit) while keeping clear of
// `obstacles`. Each obstacle whose path, from now to `horizon` seconds ahead,
// brings it within the two radii of the agent walking at `speed` pushes the
// direction sideways from where it would meet, harder the closer and sooner.
// Pure arithmetic: the game still collides with the level. With nothing in the
// way it is `desired` normalised; a zero `desired` gives zero.
[[nodiscard]] inline Vec2 SteerAround(const Vec2 pos, const Vec2 desired,
                                      const double radius, const double speed,
                                      const std::vector<Obstacle>& obstacles,
                                      const double horizon = 1.5) {
  const double len = std::hypot(desired.x, desired.y);
  if (len < 1e-9) return {0, 0};
  const Vec2 dir{desired.x / len, desired.y / len};
  Vec2 push{0, 0};
  for (const Obstacle& o : obstacles) {
    // Relative motion: the obstacle as seen from the agent walking `dir`.
    const Vec2 rel{o.pos.x - pos.x, o.pos.y - pos.y};
    const Vec2 rv{o.vel.x - dir.x * speed, o.vel.y - dir.y * speed};
    const double rv2 = rv.x * rv.x + rv.y * rv.y;
    double t = rv2 > 1e-9 ? -(rel.x * rv.x + rel.y * rv.y) / rv2 : 0.0;
    t = std::clamp(t, 0.0, horizon);
    const Vec2 meet{rel.x + rv.x * t, rel.y + rv.y * t};
    const double gap = std::hypot(meet.x, meet.y);
    const double need = radius + o.radius;
    if (gap >= need) continue;
    // Away from where they meet; dead centre: to the agent's left.
    Vec2 away =
        gap > 1e-6 ? Vec2{-meet.x / gap, -meet.y / gap} : Vec2{-dir.y, dir.x};
    const double weight = (need - gap) / need * (1.0 - t / (horizon + 1e-9));
    push.x += away.x * weight;
    push.y += away.y * weight;
  }
  Vec2 out{dir.x + push.x * 2.0, dir.y + push.y * 2.0};
  const double n = std::hypot(out.x, out.y);
  if (n < 1e-9) return dir;
  return {out.x / n, out.y / n};
}

// A member of a group, for `FlockSteer`.
struct FlockMember {
  Vec2 pos;
  Vec2 vel;
};

// Weights and reach of the three flocking rules.
struct FlockParams {
  double neighbour_radius = 3.0;   // who counts as a neighbour
  double separation_radius = 1.0;  // closer than this pushes apart
  double separation = 1.5;
  double alignment = 0.6;
  double cohesion = 0.4;
};

// The velocity change (a direction-and-strength, not unit) member `self` of
// `group` wants from the three classic rules: keep apart from those closer
// than `separation_radius`, match the neighbours' velocity, drift toward their
// centre. Add it to the member's own goal direction (for example
// `SteerAround`'s) and normalise. Zero when there are no neighbours.
[[nodiscard]] inline Vec2 FlockSteer(const std::vector<FlockMember>& group,
                                     const size_t self,
                                     const FlockParams& p = {}) {
  if (self >= group.size()) return {0, 0};
  const FlockMember& me = group[self];
  Vec2 apart{0, 0}, vel{0, 0}, centre{0, 0};
  int count = 0;
  for (size_t i = 0; i < group.size(); ++i) {
    if (i == self) continue;
    const double dx = group[i].pos.x - me.pos.x, dy = group[i].pos.y - me.pos.y;
    const double d = std::hypot(dx, dy);
    if (d > p.neighbour_radius) continue;
    ++count;
    vel.x += group[i].vel.x;
    vel.y += group[i].vel.y;
    centre.x += dx;
    centre.y += dy;
    if (d < p.separation_radius) {
      // Away from the neighbour, stronger the closer; on top: to the right.
      const double w = (1.0 - d / p.separation_radius);
      const Vec2 away = d > 1e-6 ? Vec2{-dx / d, -dy / d} : Vec2{1, 0};
      apart.x += away.x * w;
      apart.y += away.y * w;
    }
  }
  if (count == 0) return {0, 0};
  const double inv = 1.0 / count;
  return {apart.x * p.separation + (vel.x * inv - me.vel.x) * p.alignment +
              centre.x * inv * p.cohesion,
          apart.y * p.separation + (vel.y * inv - me.vel.y) * p.alignment +
              centre.y * inv * p.cohesion};
}

// Formations: where the members of a group stand relative to an anchor (a
// leader, or a point the group is walking to).
enum class Formation {
  kLine,    // abreast, side by side, centred on the anchor
  kColumn,  // single file behind the anchor (slot 0 is the anchor)
  kWedge,   // a V: slot 0 at the anchor, pairs falling back to each side
  kCircle,  // a ring around the anchor, slot 0 in front
  kGrid,    // ranks behind the anchor, as square as the count allows
};

// `count` slot positions for `kind` around `anchor`, the group facing
// `forward` (any non-zero direction; the right-hand side is `(-f.y, f.x)`
// turned the way the map's z grows), `spacing` metres apart.
[[nodiscard]] inline std::vector<Vec2> FormationSlots(
    const Formation kind, const size_t count, const Vec2 anchor, Vec2 forward,
    const double spacing = 1.2) {
  std::vector<Vec2> slots;
  slots.reserve(count);
  const double len = std::hypot(forward.x, forward.y);
  forward = len > 1e-9 ? Vec2{forward.x / len, forward.y / len} : Vec2{0, 1};
  const Vec2 side{-forward.y, forward.x};
  const auto at = [&](const double ahead, const double across) {
    return Vec2{anchor.x + forward.x * ahead + side.x * across,
                anchor.y + forward.y * ahead + side.y * across};
  };
  const auto n = static_cast<double>(count);
  for (size_t i = 0; i < count; ++i) {
    const auto k = static_cast<double>(i);
    switch (kind) {
      case Formation::kLine:
        slots.push_back(at(0, (k - (n - 1) / 2) * spacing));
        break;
      case Formation::kColumn:
        slots.push_back(at(-k * spacing, 0));
        break;
      case Formation::kWedge: {
        const double rank = std::floor((k + 1) / 2);
        const double sign = i % 2 == 1 ? -1.0 : 1.0;
        slots.push_back(at(-rank * spacing, sign * rank * spacing));
        break;
      }
      case Formation::kCircle: {
        const double radius =
            std::max(spacing, spacing * n / (2 * 3.14159265358979));
        const double angle = 2 * 3.14159265358979 * k / std::max(1.0, n);
        slots.push_back(at(std::cos(angle) * radius, std::sin(angle) * radius));
        break;
      }
      case Formation::kGrid: {
        const double columns = std::ceil(std::sqrt(n));
        const double row = std::floor(k / columns), col = k - row * columns;
        slots.push_back(
            at(-row * spacing, (col - (columns - 1) / 2) * spacing));
        break;
      }
    }
  }
  return slots;
}

// Which slot each member takes (result[i] is member i's slot index): the
// closest free pairs first, so members do not cross over to get to the slots.
// Needs as many slots as members; surplus slots stay empty.
[[nodiscard]] inline std::vector<size_t> AssignSlots(
    const std::vector<Vec2>& members, const std::vector<Vec2>& slots) {
  const size_t none = static_cast<size_t>(-1);
  std::vector<size_t> result(members.size(), none);
  std::vector<bool> taken(slots.size(), false);
  for (size_t round = 0; round < std::min(members.size(), slots.size());
       ++round) {
    double best = std::numeric_limits<double>::infinity();
    size_t bm = none, bs = none;
    for (size_t m = 0; m < members.size(); ++m) {
      if (result[m] != none) continue;
      for (size_t s = 0; s < slots.size(); ++s) {
        if (taken[s]) continue;
        const double d =
            std::hypot(members[m].x - slots[s].x, members[m].y - slots[s].y);
        if (d < best) {
          best = d;
          bm = m;
          bs = s;
        }
      }
    }
    if (bm == none) break;
    result[bm] = bs;
    taken[bs] = true;
  }
  return result;
}

// The velocity that takes a member to its slot: toward it at `speed`,
// slowing inside `arrive_radius` so it stops on the slot. Add `FlockSteer`'s
// separation and `SteerAround` for the rest.
[[nodiscard]] inline Vec2 FormationSteer(const Vec2 pos, const Vec2 slot,
                                         const double speed,
                                         const double arrive_radius = 1.0) {
  const double dx = slot.x - pos.x, dy = slot.y - pos.y;
  const double d = std::hypot(dx, dy);
  if (d < 1e-6) return {0, 0};
  const double v = speed * std::min(1.0, d / std::max(1e-6, arrive_radius));
  return {dx / d * v, dy / d * v};
}
}  // namespace mythos

#endif  // MYTHOS_ENGINE_WORLD_NAV_H_