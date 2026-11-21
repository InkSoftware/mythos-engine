#include "engine/world/movement.h"

#include <cmath>

namespace mythos {
namespace {
// Approximately 1/sqrt(2), to normalize a diagonal input.
constexpr double kDiagonalScale = 0.7071;
}

Vec2 WalkStep(const double angle, const double forward, const double side,
              const double distance) {
  const double dir_x = std::cos(angle), dir_y = std::sin(angle);
  const double right_x = -dir_y, right_y = dir_x;
  double dx = (dir_x * forward + right_x * side) * distance;
  double dy = (dir_y * forward + right_y * side) * distance;
  if (forward != 0 && side != 0) {
    dx *= kDiagonalScale;
    dy *= kDiagonalScale;
  }
  return {.x = dx, .y = dy};
}

bool CanOccupy(const Level& level, const double x, const double y,
               const double radius) {
  return !level.BoxBlocked(x - radius, y - radius, x + radius, y + radius);
}

void MoveWithCollision(const Level& level, Vec2& pos, const double dx,
                       const double dy, const double radius) {
  const double new_x = pos.x + dx;
  if (CanOccupy(level, new_x, pos.y, radius)) pos.x = new_x;
  const double new_y = pos.y + dy;
  if (CanOccupy(level, pos.x, new_y, radius)) pos.y = new_y;
}
}
