#ifndef MYTHOS_ENGINE_WORLD_MOVEMENT_H_
#define MYTHOS_ENGINE_WORLD_MOVEMENT_H_

#include <vector>

#include "engine/core/math.h"
#include "engine/world/level.h"

// Collision movement for a round body on a grid level.
namespace mythos {
// True if a body of the given radius centerd at (x, y) overlaps nothing
// blocked, with thin walls tested precisely (Level::BoxBlocked on the bounding
// square).
bool CanOccupy(const Level& level, double x, double y, double r);

// Moves pos by (dx, dy) one axis at a time, so the body slides along walls
// instead of stopping dead.
void MoveWithCollision(const Level& level, Vec2& pos, double dx, double dy,
                       double r);

// The displacement of a body walking for distance at angle (radians; 0 is +x,
// the map plane is x, z) with forward and side each in [-1, 1] (side is to the
// right). Walking diagonally is no faster than straight.
Vec2 WalkStep(double angle, double forward, double side, double distance);
}

#endif
