#pragma once
// Reads required values out of a parsed JSON document by dotted path ("player.walkSpeed").
// The first problem is kept, so a loader can read everything and check ok() once.

#include <string>
#include <utility>

#include "engine/core/json.h"
#include "engine/core/math.h"

namespace mythos::json {

class Reader {
public:
    Reader(const Value& root, std::string file) : root_(root), file_(std::move(file)) {}

    const Value* at(const std::string& path) {
        const Value* v = &root_;
        size_t start = 0;
        while (v && start <= path.size()) {
            const size_t dot = path.find('.', start);
            const std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            v = v->find(key);
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        if (!v) fail("missing \"" + path + "\"");
        return v;
    }
    double number(const std::string& path) {
        const Value* v = at(path);
        if (v && !v->isNumber()) fail("\"" + path + "\" must be a number");
        return v && v->isNumber() ? v->asNumber() : 0.0;
    }
    int integer(const std::string& path) {
        const double d = number(path);
        if (d != std::floor(d)) fail("\"" + path + "\" must be a whole number");
        return static_cast<int>(d);
    }
    // Degrees in the file, radians in the game. (deg / 180) * pi keeps -90 exactly -pi/2.
    double angle(const std::string& path) { return number(path) / 180.0 * kPi; }
    std::string string(const std::string& path) {
        const Value* v = at(path);
        if (v && !v->isString()) fail("\"" + path + "\" must be a string");
        return v && v->isString() ? v->asString() : std::string();
    }
    void cell(const std::string& path, double& x, double& y) {
        const Value* v = at(path);
        if (v && (!v->isArray() || v->items().size() != 2 || !v->items()[0].isNumber() || !v->items()[1].isNumber())) {
            fail("\"" + path + "\" must be [x, y]");
            return;
        }
        if (v) {
            x = v->items()[0].asNumber();
            y = v->items()[1].asNumber();
        }
    }
    void cell(const std::string& path, int& x, int& y) {
        double dx = 0, dy = 0;
        cell(path, dx, dy);
        x = static_cast<int>(dx);
        y = static_cast<int>(dy);
    }
    void fail(const std::string& what) {
        if (error_.empty()) error_ = file_ + ": " + what;
    }
    bool ok() const { return error_.empty(); }
    const std::string& error() const { return error_; }

private:
    const Value& root_;
    std::string file_;
    std::string error_;
};

}  // namespace mythos::json
