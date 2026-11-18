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

std::string trimmed(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
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
    for (const auto& r : rows) cells_.insert(cells_.end(), r.begin(), r.end());
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
            std::istringstream ls(line);
            Placement p;
            if (!(ls >> p.x >> p.y >> p.model >> p.yawDeg)) {
                if (error) *error = "bad furniture line: " + line;
                return false;
            }
            furniture.push_back(p);
        } else if (section == "[decor]") {
            // A small object that blocks nothing: x y model yaw [offX offZ lift] (metres from the cell centre; lift above the floor).
            std::istringstream ls(line);
            Placement p;
            p.solid = false;
            if (!(ls >> p.x >> p.y >> p.model >> p.yawDeg)) {
                if (error) *error = "bad decor line: " + line;
                return false;
            }
            ls >> p.offX >> p.offZ >> p.lift;  // optional
            decor.push_back(p);
        }
    }
    if (!parse(rows, error)) return false;
    for (const auto& p : furniture) {
        if (!inBounds(p.x, p.y)) {
            if (error) *error = "furniture outside the map: " + p.model;
            return false;
        }
        solid_[static_cast<size_t>(p.y) * w_ + p.x] = 1;
    }
    for (const auto& p : decor) {
        if (!inBounds(p.x, p.y)) {
            if (error) *error = "decor outside the map: " + p.model;
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

void Level::set(int x, int y, char c) {
    if (!inBounds(x, y)) return;
    char& cell = cells_[static_cast<size_t>(y) * w_ + x];
    if (cell == c) return;
    cell = c;
    revision_ = nextRevision();
}

bool Level::passable(int x, int y) const {
    if (!inBounds(x, y)) return false;
    return !tiles_->solid(at(x, y)) && solid_[static_cast<size_t>(y) * w_ + x] == 0;
}

bool Level::addFurniture(const Placement& p) {
    if (!inBounds(p.x, p.y) || (p.solid && !passable(p.x, p.y))) return false;
    furniture_.push_back(p);
    if (p.solid) solid_[static_cast<size_t>(p.y) * w_ + p.x] = 1;
    revision_ = nextRevision();
    return true;
}

bool Level::removePlacement(const std::string& id) {
    if (id.empty()) return false;
    const auto it = std::find_if(furniture_.begin(), furniture_.end(), [&](const Placement& p) { return p.id == id; });
    if (it == furniture_.end()) return false;
    const int x = it->x, y = it->y;
    furniture_.erase(it);
    // The cell stays solid while any other furniture stands on it.
    solid_[static_cast<size_t>(y) * w_ + x] = furnitureAt(x, y) ? 1 : 0;
    revision_ = nextRevision();
    return true;
}

const Placement* Level::furnitureAt(int x, int y) const {
    for (const auto& p : furniture_)
        if (p.solid && p.x == x && p.y == y) return &p;
    return nullptr;
}

bool Level::find(char c, int& x, int& y) const {
    for (int j = 0; j < h_; ++j)
        for (int i = 0; i < w_; ++i)
            if (at(i, j) == c) {
                x = i;
                y = j;
                return true;
            }
    return false;
}

std::vector<std::pair<int, int>> Level::findAll(char c) const {
    std::vector<std::pair<int, int>> out;
    for (int j = 0; j < h_; ++j)
        for (int i = 0; i < w_; ++i)
            if (at(i, j) == c) out.emplace_back(i, j);
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

    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};
    for (size_t head = 0; head < queue.size(); ++head) {
        const int cur = queue[head];
        const int cx = cur % w_;
        const int cy = cur / w_;
        for (int k = 0; k < 4; ++k) {
            const int nx = cx + dx[k];
            const int ny = cy + dy[k];
            if (!passable(nx, ny)) continue;
            const int idx = ny * w_ + nx;
            if (dist[idx] != -1) continue;
            dist[idx] = dist[cur] + 1;
            queue.push_back(idx);
        }
    }
    return dist;
}

bool Level::lineOfSight(double x0, double y0, double x1, double y1) const {
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const double len = std::sqrt(dx * dx + dy * dy);
    const int steps = std::max(1, static_cast<int>(len / 0.15));
    for (int i = 1; i < steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        const int cx = static_cast<int>(std::floor(x0 + dx * t));
        const int cy = static_cast<int>(std::floor(y0 + dy * t));
        if (!passable(cx, cy)) return false;  // walls and furniture block sight
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
