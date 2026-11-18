#include "engine/world/room_doors.h"

#include <algorithm>
#include <set>
#include <utility>

namespace mythos {
namespace {

// A door lets people through (and shows as open) once it is more than half open.
constexpr double kPassableOpen = 0.5;
// regionSize stops counting at this many cells (also what a blocked start cell reports).
constexpr int kRegionCap = 400;

}  // namespace

void RoomDoors::scan(const Level& level) {
    doors_.clear();
    const TileSet& tiles = level.tiles();
    for (int y = 0; y < level.height(); ++y)
        for (int x = 0; x < level.width(); ++x) {
            const char glyph = level.at(x, y);
            if (glyph != 'N' && glyph != 'n') continue;
            Door door;
            door.x = x;
            door.y = y;
            door.alongX = tiles.solid(level.at(x, y - 1)) && tiles.solid(level.at(x, y + 1));
            door.open = glyph == 'n' ? 1.0 : 0.0;
            door.opening = glyph == 'n';
            // It swings into the room: the smaller of the two spaces the door separates.
            const int sideX = door.alongX ? 1 : 0, sideY = door.alongX ? 0 : 1;
            door.swing = regionSize(level, x + sideX, y + sideY) <= regionSize(level, x - sideX, y - sideY) ? 1 : -1;
            doors_.push_back(door);
        }
}

int RoomDoors::use(int x, int y) {
    for (auto& door : doors_) {
        if (door.x != x || door.y != y) continue;
        door.opening = !door.opening;
        return door.opening ? 1 : -1;
    }
    return 0;
}

int RoomDoors::update(Level& level, double dt, const Vec2& body, double bodyRadius, double openSeconds,
                      double closeSeconds) {
    // A door stops blocking (glyph 'n') once past half open, and blocks again once it falls below half.
    int closed = 0;
    for (auto& door : doors_) {
        if (door.opening) {
            if (door.open >= 1.0) continue;
            door.open = std::min(1.0, door.open + dt / openSeconds);
            if (door.open > kPassableOpen && level.at(door.x, door.y) == 'N') level.set(door.x, door.y, 'n');
        } else if (door.open > 0) {
            double next = std::max(0.0, door.open - dt / closeSeconds);
            if (next < kPassableOpen && level.at(door.x, door.y) == 'n') {
                if (overlaps(body, bodyRadius, door.x, door.y)) next = kPassableOpen;  // wait, ajar, until they step out
                else level.set(door.x, door.y, 'N');
            }
            door.open = next;
            if (door.open <= 0) ++closed;
        }
    }
    return closed;
}

void RoomDoors::appendStates(std::vector<DoorState>& out) const {
    for (const auto& door : doors_) out.push_back({door.x, door.y, door.alongX, door.open, door.swing});
}

int RoomDoors::regionSize(const Level& level, int x, int y) {
    if (!level.passable(x, y)) return kRegionCap;
    // Flood fill that stops at doors; cells are keyed row * 1000 + column (levels are narrower than 1000).
    std::set<int> seen{y * 1000 + x};
    std::vector<std::pair<int, int>> todo{{x, y}};
    const TileSet& tiles = level.tiles();
    while (!todo.empty() && static_cast<int>(seen.size()) < kRegionCap) {
        const auto [cellX, cellY] = todo.back();
        todo.pop_back();
        const int stepX[4] = {1, -1, 0, 0}, stepY[4] = {0, 0, 1, -1};
        for (int dir = 0; dir < 4; ++dir) {
            const int nextX = cellX + stepX[dir], nextY = cellY + stepY[dir];
            if (!level.passable(nextX, nextY) || tiles.isDoor(level.at(nextX, nextY))) continue;
            if (seen.insert(nextY * 1000 + nextX).second) todo.push_back({nextX, nextY});
        }
    }
    return static_cast<int>(seen.size());
}

bool RoomDoors::overlaps(const Vec2& pos, double radius, int x, int y) {
    // Distance from the circle's centre to the nearest point of the door's cell.
    const double nearestX = clampd(pos.x, x, x + 1.0), nearestY = clampd(pos.y, y, y + 1.0);
    const double dx = pos.x - nearestX, dy = pos.y - nearestY;
    return dx * dx + dy * dy < radius * radius;
}

}  // namespace mythos
