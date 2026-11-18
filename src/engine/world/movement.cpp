#include "engine/world/movement.h"

#include <cmath>

namespace mythos {
namespace {

// Moving diagonally covers sqrt(2) cells per axis step; scale by 1/sqrt(2) (as a 4-digit constant) to keep speed even.
constexpr double kDiagonalScale = 0.7071;
// Walking a distance field stops once less than this is left to cover.
constexpr double kMinWalkBudget = 1e-9;

// The distance-field value of a cell, or -1 outside the grid (or with no field).
int fieldAt(const std::vector<int>& field, int x, int y, int width, int height) {
    if (x < 0 || y < 0 || x >= width || y >= height || field.empty()) return -1;
    return field[static_cast<size_t>(y) * width + x];
}

}  // namespace

Vec2 walkStep(double angle, double forward, double side, double distance) {
    const double dirX = std::cos(angle), dirY = std::sin(angle);
    const double rightX = -dirY, rightY = dirX;  // matches the renderer's camera plane
    double dx = (dirX * forward + rightX * side) * distance;
    double dy = (dirY * forward + rightY * side) * distance;
    if (forward != 0 && side != 0) {  // keep diagonal speed equal to straight speed
        dx *= kDiagonalScale;
        dy *= kDiagonalScale;
    }
    return {dx, dy};
}

bool canOccupy(const Level& level, double x, double y, double radius) {
    // The body is a square of half-width `radius`: all four corners must stand on passable cells.
    const double offsets[2] = {-radius, radius};
    for (double offsetX : offsets)
        for (double offsetY : offsets)
            if (!level.passable(static_cast<int>(std::floor(x + offsetX)), static_cast<int>(std::floor(y + offsetY))))
                return false;
    return true;
}

void moveWithCollision(const Level& level, Vec2& pos, double dx, double dy, double radius) {
    // One axis at a time, so he slides along a wall instead of stopping against it.
    const double newX = pos.x + dx;
    if (canOccupy(level, newX, pos.y, radius)) pos.x = newX;
    const double newY = pos.y + dy;
    if (canOccupy(level, pos.x, newY, radius)) pos.y = newY;
}

void walkField(const Level& level, const std::vector<int>& field, Vec2& pos, FieldWalker& walker, double distance) {
    if (field.empty()) return;
    const int width = level.width(), height = level.height();
    double budget = distance;

    // Each new target has a strictly smaller distance value, so this terminates.
    while (budget > kMinWalkBudget) {
        if (!walker.hasTarget) {
            const int cellX = static_cast<int>(std::floor(pos.x));
            const int cellY = static_cast<int>(std::floor(pos.y));
            const int here = fieldAt(field, cellX, cellY, width, height);
            if (here <= 0) return;  // reached the goal, or unreachable

            // Step to the neighbouring cell closest to the goal.
            const int stepX[4] = {1, -1, 0, 0};
            const int stepY[4] = {0, 0, 1, -1};
            int bestDistance = here, bestX = -1, bestY = -1;
            for (int dir = 0; dir < 4; ++dir) {
                const int nextX = cellX + stepX[dir], nextY = cellY + stepY[dir];
                const int distance = fieldAt(field, nextX, nextY, width, height);
                if (distance >= 0 && distance < bestDistance) {
                    bestDistance = distance;
                    bestX = nextX;
                    bestY = nextY;
                }
            }
            if (bestX < 0) return;
            walker.target = {bestX + 0.5, bestY + 0.5};
            walker.hasTarget = true;
        }

        const double dx = walker.target.x - pos.x;
        const double dy = walker.target.y - pos.y;
        const double remaining = std::sqrt(dx * dx + dy * dy);
        if (remaining <= budget) {
            pos = walker.target;
            budget -= remaining;
            walker.hasTarget = false;
        } else {
            pos.x += dx / remaining * budget;
            pos.y += dy / remaining * budget;
            budget = 0;
        }
    }
}

}  // namespace mythos
