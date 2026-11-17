#include "engine/world/movement.h"

#include <cmath>

namespace mythos {
namespace {

int fieldAt(const std::vector<int>& field, int x, int y, int w, int h) {
    if (x < 0 || y < 0 || x >= w || y >= h || field.empty()) return -1;
    return field[static_cast<size_t>(y) * w + x];
}

}  // namespace

Vec2 walkStep(double angle, double fwd, double side, double distance) {
    const double dirX = std::cos(angle), dirY = std::sin(angle);
    const double rightX = -dirY, rightY = dirX;  // matches the renderer's camera plane
    double dx = (dirX * fwd + rightX * side) * distance;
    double dy = (dirY * fwd + rightY * side) * distance;
    if (fwd != 0 && side != 0) {  // keep diagonal speed equal to straight speed
        dx *= 0.7071;
        dy *= 0.7071;
    }
    return {dx, dy};
}

bool canOccupy(const Level& level, double x, double y, double r) {
    const double offs[2] = {-r, r};
    for (double ox : offs)
        for (double oy : offs)
            if (!level.passable(static_cast<int>(std::floor(x + ox)), static_cast<int>(std::floor(y + oy))))
                return false;
    return true;
}

void moveWithCollision(const Level& level, Vec2& pos, double dx, double dy, double r) {
    const double nx = pos.x + dx;
    if (canOccupy(level, nx, pos.y, r)) pos.x = nx;
    const double ny = pos.y + dy;
    if (canOccupy(level, pos.x, ny, r)) pos.y = ny;
}

void walkField(const Level& level, const std::vector<int>& field, Vec2& pos, FieldWalker& walker, double distance) {
    if (field.empty()) return;
    const int w = level.width(), h = level.height();
    double budget = distance;

    // Each new target has a strictly smaller distance value, so this terminates.
    while (budget > 1e-9) {
        if (!walker.hasTarget) {
            const int tx = static_cast<int>(std::floor(pos.x));
            const int ty = static_cast<int>(std::floor(pos.y));
            const int here = fieldAt(field, tx, ty, w, h);
            if (here <= 0) return;  // reached the goal, or unreachable

            const int dxs[4] = {1, -1, 0, 0};
            const int dys[4] = {0, 0, 1, -1};
            int best = here, bx = -1, by = -1;
            for (int k = 0; k < 4; ++k) {
                const int nx = tx + dxs[k], ny = ty + dys[k];
                const int v = fieldAt(field, nx, ny, w, h);
                if (v >= 0 && v < best) {
                    best = v;
                    bx = nx;
                    by = ny;
                }
            }
            if (bx < 0) return;
            walker.target = {bx + 0.5, by + 0.5};
            walker.hasTarget = true;
        }

        const double dx = walker.target.x - pos.x;
        const double dy = walker.target.y - pos.y;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d <= budget) {
            pos = walker.target;
            budget -= d;
            walker.hasTarget = false;
        } else {
            pos.x += dx / d * budget;
            pos.y += dy / d * budget;
            budget = 0;
        }
    }
}

}  // namespace mythos
