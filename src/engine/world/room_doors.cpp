#include "engine/world/room_doors.h"

#include <algorithm>
#include <set>
#include <utility>

namespace mythos {

void RoomDoors::scan(const Level& level) {
    doors_.clear();
    const TileSet& t = level.tiles();
    for (int y = 0; y < level.height(); ++y)
        for (int x = 0; x < level.width(); ++x) {
            const char c = level.at(x, y);
            if (c != 'N' && c != 'n') continue;
            Door d;
            d.x = x;
            d.y = y;
            d.alongX = t.solid(level.at(x, y - 1)) && t.solid(level.at(x, y + 1));
            d.open = c == 'n' ? 1.0 : 0.0;
            d.opening = c == 'n';
            // It swings into the room: the smaller of the two spaces the door separates.
            const int ax = d.alongX ? 1 : 0, ay = d.alongX ? 0 : 1;
            d.swing = regionSize(level, x + ax, y + ay) <= regionSize(level, x - ax, y - ay) ? 1 : -1;
            doors_.push_back(d);
        }
}

int RoomDoors::use(int x, int y) {
    for (auto& d : doors_) {
        if (d.x != x || d.y != y) continue;
        d.opening = !d.opening;
        return d.opening ? 1 : -1;
    }
    return 0;
}

int RoomDoors::update(Level& level, double dt, const Vec2& body, double bodyRadius, double openSeconds,
                      double closeSeconds) {
    int closed = 0;
    for (auto& d : doors_) {
        if (d.opening) {
            if (d.open >= 1.0) continue;
            d.open = std::min(1.0, d.open + dt / openSeconds);
            if (d.open > 0.5 && level.at(d.x, d.y) == 'N') level.set(d.x, d.y, 'n');
        } else if (d.open > 0) {
            double next = std::max(0.0, d.open - dt / closeSeconds);
            if (next < 0.5 && level.at(d.x, d.y) == 'n') {
                if (overlaps(body, bodyRadius, d.x, d.y)) next = 0.5;  // wait, ajar, until they step out
                else level.set(d.x, d.y, 'N');
            }
            d.open = next;
            if (d.open <= 0) ++closed;
        }
    }
    return closed;
}

void RoomDoors::appendStates(std::vector<DoorState>& out) const {
    for (const auto& d : doors_) out.push_back({d.x, d.y, d.alongX, d.open, d.swing});
}

int RoomDoors::regionSize(const Level& level, int x, int y) {
    const int cap = 400;
    if (!level.passable(x, y)) return cap;
    std::set<int> seen{y * 1000 + x};
    std::vector<std::pair<int, int>> todo{{x, y}};
    const TileSet& t = level.tiles();
    while (!todo.empty() && static_cast<int>(seen.size()) < cap) {
        const auto [cx, cy] = todo.back();
        todo.pop_back();
        const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) {
            const int nx = cx + dx[k], ny = cy + dy[k];
            if (!level.passable(nx, ny) || t.isDoor(level.at(nx, ny))) continue;
            if (seen.insert(ny * 1000 + nx).second) todo.push_back({nx, ny});
        }
    }
    return static_cast<int>(seen.size());
}

bool RoomDoors::overlaps(const Vec2& pos, double r, int x, int y) {
    const double nx = clampd(pos.x, x, x + 1.0), ny = clampd(pos.y, y, y + 1.0);
    const double dx = pos.x - nx, dy = pos.y - ny;
    return dx * dx + dy * dy < r * r;
}

}  // namespace mythos
