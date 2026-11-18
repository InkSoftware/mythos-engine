#include "engine/world/level.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "engine/core/vfs.h"

namespace mythos {
namespace {

// Every change anywhere gets a new number, so a fresh copy of a level never looks like an old one.
uint64_t nextRevision() {
    static uint64_t counter = 0;
    return ++counter;
}

// Line-of-sight samples the segment every this many cells (metres): fine enough not to skip over a one-cell wall.
constexpr double kSightStep = 0.15;

std::string trimmed(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) return "";
    const size_t last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

bool readFile(const std::string& path, std::string& out) {
    return vfs::readFile(path, out);
}

}  // namespace

bool Level::parse(const std::vector<std::string>& rows, std::string* error) {
    if (rows.empty()) {
        if (error) *error = "map is empty";
        return false;
    }
    const size_t width = rows[0].size();
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].size() != width) {
            if (error) *error = "row " + std::to_string(i) + " has a different length";
            return false;
        }
    }
    w_ = static_cast<int>(width);
    h_ = static_cast<int>(rows.size());
    cells_.clear();
    cells_.reserve(static_cast<size_t>(w_) * static_cast<size_t>(h_));
    for (const auto& row : rows) cells_.insert(cells_.end(), row.begin(), row.end());
    solid_.assign(cells_.size(), 0);
    furniture_.clear();
    revision_ = nextRevision();
    return true;
}

bool Level::parseText(const std::string& text, std::string* error) {
    std::vector<std::string> rows;
    std::vector<Placement> furniture, decor;
    std::string section;
    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
        const std::string line = trimmed(raw);
        if (line.empty() || line[0] == ';') continue;
        if (line.front() == '[') {
            section = line;
            continue;
        }
        if (section == "[map]") {
            rows.push_back(line);
        } else if (section == "[furniture]") {
            std::istringstream fields(line);
            Placement placement;
            if (!(fields >> placement.x >> placement.y >> placement.model >> placement.yawDeg)) {
                if (error) *error = "bad furniture line: " + line;
                return false;
            }
            furniture.push_back(placement);
        } else if (section == "[decor]") {
            // A small object that blocks nothing: x y model yaw [offX offZ lift] (metres from the cell centre; lift above the floor).
            std::istringstream fields(line);
            Placement placement;
            placement.solid = false;
            if (!(fields >> placement.x >> placement.y >> placement.model >> placement.yawDeg)) {
                if (error) *error = "bad decor line: " + line;
                return false;
            }
            fields >> placement.offX >> placement.offZ >> placement.lift;  // optional
            decor.push_back(placement);
        }
    }
    if (!parse(rows, error)) return false;
    for (const auto& placement : furniture) {
        if (!inBounds(placement.x, placement.y)) {
            if (error) *error = "furniture outside the map: " + placement.model;
            return false;
        }
        solid_[static_cast<size_t>(placement.y) * w_ + placement.x] = 1;
    }
    for (const auto& placement : decor) {
        if (!inBounds(placement.x, placement.y)) {
            if (error) *error = "decor outside the map: " + placement.model;
            return false;
        }
    }
    furniture_ = furniture;
    furniture_.insert(furniture_.end(), decor.begin(), decor.end());
    return true;
}

char Level::at(int x, int y) const {
    return inBounds(x, y) ? cells_[static_cast<size_t>(y) * w_ + x] : '#';
}

void Level::set(int x, int y, char glyph) {
    if (!inBounds(x, y)) return;
    char& cell = cells_[static_cast<size_t>(y) * w_ + x];
    if (cell == glyph) return;
    cell = glyph;
    revision_ = nextRevision();
}

bool Level::passable(int x, int y) const {
    if (!inBounds(x, y)) return false;
    return !tiles_->solid(at(x, y)) && solid_[static_cast<size_t>(y) * w_ + x] == 0;
}

bool Level::addFurniture(const Placement& placement) {
    if (!inBounds(placement.x, placement.y) || (placement.solid && !passable(placement.x, placement.y))) return false;
    furniture_.push_back(placement);
    if (placement.solid) solid_[static_cast<size_t>(placement.y) * w_ + placement.x] = 1;
    revision_ = nextRevision();
    return true;
}

bool Level::removePlacement(const std::string& id) {
    if (id.empty()) return false;
    const auto it = std::find_if(furniture_.begin(), furniture_.end(), [&](const Placement& placement) { return placement.id == id; });
    if (it == furniture_.end()) return false;
    const int x = it->x, y = it->y;
    furniture_.erase(it);
    // The cell stays solid while any other furniture stands on it.
    solid_[static_cast<size_t>(y) * w_ + x] = furnitureAt(x, y) ? 1 : 0;
    revision_ = nextRevision();
    return true;
}

const Placement* Level::furnitureAt(int x, int y) const {
    for (const auto& placement : furniture_)
        if (placement.solid && placement.x == x && placement.y == y) return &placement;
    return nullptr;
}

bool Level::find(char glyph, int& x, int& y) const {
    for (int row = 0; row < h_; ++row)
        for (int col = 0; col < w_; ++col)
            if (at(col, row) == glyph) {
                x = col;
                y = row;
                return true;
            }
    return false;
}

std::vector<std::pair<int, int>> Level::findAll(char glyph) const {
    std::vector<std::pair<int, int>> out;
    for (int row = 0; row < h_; ++row)
        for (int col = 0; col < w_; ++col)
            if (at(col, row) == glyph) out.emplace_back(col, row);
    return out;
}

std::vector<int> Level::distanceField(int gx, int gy) const {
    std::vector<int> dist(static_cast<size_t>(w_) * h_, -1);
    if (!inBounds(gx, gy) || !passable(gx, gy)) return dist;

    std::vector<int> queue;
    queue.reserve(dist.size());
    const int start = gy * w_ + gx;
    dist[start] = 0;
    queue.push_back(start);

    const int stepX[4] = {1, -1, 0, 0};
    const int stepY[4] = {0, 0, 1, -1};
    for (size_t head = 0; head < queue.size(); ++head) {
        const int current = queue[head];
        const int cellX = current % w_;
        const int cellY = current / w_;
        for (int dir = 0; dir < 4; ++dir) {
            const int nextX = cellX + stepX[dir];
            const int nextY = cellY + stepY[dir];
            if (!passable(nextX, nextY)) continue;
            const int next = nextY * w_ + nextX;
            if (dist[next] != -1) continue;
            dist[next] = dist[current] + 1;
            queue.push_back(next);
        }
    }
    return dist;
}

bool Level::lineOfSight(double x0, double y0, double x1, double y1) const {
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const double length = std::sqrt(dx * dx + dy * dy);
    const int steps = std::max(1, static_cast<int>(length / kSightStep));
    for (int step = 1; step < steps; ++step) {
        const double along = static_cast<double>(step) / steps;
        const int cellX = static_cast<int>(std::floor(x0 + dx * along));
        const int cellY = static_cast<int>(std::floor(y0 + dy * along));
        if (!passable(cellX, cellY)) return false;  // walls and furniture block sight
    }
    return true;
}

bool Level::loadFile(const std::string& path, std::string* error) {
    std::string text;
    if (!readFile(path, text)) {
        if (error) *error = "missing level file " + path;
        return false;
    }
    std::string err;
    if (!parseText(text, &err)) {
        if (error) *error = path + ": " + err;
        return false;
    }
    return true;
}

}  // namespace mythos
