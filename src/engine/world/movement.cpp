#include "engine/world/movement.h"

#include <cmath>

namespace mythos {
namespace {

// Moving diagonally covers sqrt(2) cells per axis step; scale by 1/sqrt(2) (as
// a 4-digit constant) to keep speed even.
constexpr double kDiagonalScale = 0.7071;

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
  // The body is a square of half-width `radius`: it may touch no wall (a thin
  // wall only where its slab is) and no furniture.
  return !level.BoxBlocked(x - radius, y - radius, x + radius, y + radius);
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

}  // namespace mythos
