#include "engine/world/movement.h"

#include <cmath>

namespace mythos {
namespace {

// Moving diagonally covers sqrt(2) cells per axis step; scale by 1/sqrt(2) (as
// a 4-digit constant) to keep speed even.
constexpr double kDiagonalScale = 0.7071;
// Walking a distance field stops once less than this is left to cover.
constexpr double kMinWalkBudget = 1e-9;

// The distance-field value of a cell, or -1 outside the grid (or with no
// field).
int FieldAt(const std::vector<int>& field, int x, int y, int width,
            int height) {
  if (x < 0 || y < 0 || x >= width || y >= height || field.empty()) return -1;
  return field[static_cast<size_t>(y) * width + x];
}

}  // namespace

Vec2 WalkStep(double angle, double forward, double side, double distance) {
  const double dir_x = std::cos(angle), dir_y = std::sin(angle);
  const double right_x = -dir_y,
               right_y = dir_x;  // matches the renderer's camera plane
  double dx = (dir_x * forward + right_x * side) * distance;
  double dy = (dir_y * forward + right_y * side) * distance;
  if (forward != 0 &&
      side != 0) {  // keep diagonal speed equal to straight speed
    dx *= kDiagonalScale;
    dy *= kDiagonalScale;
  }
  return {.x = dx, .y = dy};
}

bool CanOccupy(const Level& level, double x, double y, double radius) {
  // The body is a square of half-width `radius`: all four corners must stand on
  // passable cells.
  const double offsets[2] = {-radius, radius};
  for (const double offset_x : offsets) {
    for (const double offset_y : offsets) {
      if (!level.Passable(static_cast<int>(std::floor(x + offset_x)),
                          static_cast<int>(std::floor(y + offset_y)))) {
        return false;
      }
    }
  }
  return true;
}

void MoveWithCollision(const Level& level, Vec2& pos, double dx, double dy,
                       double radius) {
  // One axis at a time, so he slides along a wall instead of stopping against
  // it.
  const double new_x = pos.x + dx;
  if (CanOccupy(level, new_x, pos.y, radius)) pos.x = new_x;
  const double new_y = pos.y + dy;
  if (CanOccupy(level, pos.x, new_y, radius)) pos.y = new_y;
}

void WalkField(const Level& level, const std::vector<int>& field, Vec2& pos,
               FieldWalker& walker, double distance) {
  if (field.empty()) return;
  const int width = level.Width(), height = level.Height();
  double budget = distance;

  // Each new target has a strictly smaller distance value, so this terminates.
  while (budget > kMinWalkBudget) {
    if (!walker.has_target) {
      const int cell_x = static_cast<int>(std::floor(pos.x));
      const int cell_y = static_cast<int>(std::floor(pos.y));
      const int here = FieldAt(field, cell_x, cell_y, width, height);
      if (here <= 0) return;  // reached the goal, or unreachable

      // Step to the neighbouring cell closest to the goal.
      const int step_x[4] = {1, -1, 0, 0};
      const int step_y[4] = {0, 0, 1, -1};
      int best_distance = here, best_x = -1, best_y = -1;
      for (int dir = 0; dir < 4; ++dir) {
        const int next_x = cell_x + step_x[dir], next_y = cell_y + step_y[dir];
        const int distance = FieldAt(field, next_x, next_y, width, height);
        if (distance >= 0 && distance < best_distance) {
          best_distance = distance;
          best_x = next_x;
          best_y = next_y;
        }
      }
      if (best_x < 0) return;
      walker.target = {.x = best_x + 0.5, .y = best_y + 0.5};
      walker.has_target = true;
    }

    const double dx = walker.target.x - pos.x;
    const double dy = walker.target.y - pos.y;
    const double remaining = std::sqrt(dx * dx + dy * dy);
    if (remaining <= budget) {
      pos = walker.target;
      budget -= remaining;
      walker.has_target = false;
    } else {
      pos.x += dx / remaining * budget;
      pos.y += dy / remaining * budget;
      budget = 0;
    }
  }
}

}  // namespace mythos
