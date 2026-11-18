#ifndef MYTHOS_ENGINE_WORLD_MOVEMENT_H_
#define MYTHOS_ENGINE_WORLD_MOVEMENT_H_
// Moving things around a grid level: a round body sliding against walls and
// furniture, and an agent walking down a BFS distance field
// (Level::distanceField) towards its goal.

#include <vector>

#include "engine/core/math.h"
#include "engine/world/level.h"

namespace mythos {

// True if a square body of half-size r centred at (x, y) touches only passable
// cells.
bool CanOccupy(const Level& level, double x, double y, double r);

// Moves pos by (dx, dy), one axis at a time, so the body slides along walls.
void MoveWithCollision(const Level& level, Vec2& pos, double dx, double dy,
                       double r);

// The step of a body walking for `distance` at `angle` (radians; 0 is +x), with
// fwd and side each in [-1, 1] (side is to the right). Walking diagonally is no
// faster than straight.
Vec2 WalkStep(double angle, double forward, double side, double distance);

// An agent walking cell centre to cell centre down a distance field.
struct FieldWalker {
  Vec2 target;  // the cell centre it is walking to
  bool has_target = false;
};

// Advances pos by up to `distance`, always stepping to the neighbouring cell
// with the lowest field value. Stops at the goal (value 0) or when the field
// gives no way on.
void WalkField(const Level& level, const std::vector<int>& field, Vec2& pos,
               FieldWalker& walker, double distance);

}  // namespace mythos

#endif  // MYTHOS_ENGINE_WORLD_MOVEMENT_H_
