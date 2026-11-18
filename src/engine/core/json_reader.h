#ifndef MYTHOS_ENGINE_CORE_JSON_READER_H_
#define MYTHOS_ENGINE_CORE_JSON_READER_H_
// Reads required values out of a parsed JSON document by dotted path
// ("player.walkSpeed"). The first problem is kept, so a loader can read
// everything and check ok() once.

#include <string>
#include <utility>

#include "engine/core/json.h"
#include "engine/core/math.h"

namespace mythos::json {

class Reader {
 public:
  Reader(const Value& root, std::string file)
      : root_(root), file_(std::move(file)) {}

  const Value* At(const std::string& path) {
    const Value* node = &root_;
    size_t start = 0;
    while (node && start <= path.size()) {
      const size_t dot = path.find('.', start);
      const std::string key = path.substr(
          start, dot == std::string::npos ? std::string::npos : dot - start);
      node = Find(*node, key);
      if (dot == std::string::npos) break;
      start = dot + 1;
    }
    if (!node) Fail("missing \"" + path + "\"");
    return node;
  }
  double Number(const std::string& path) {
    const Value* v = At(path);
    if (v && !v->is_number()) Fail("\"" + path + "\" must be a number");
    return v && v->is_number() ? v->get<double>() : 0.0;
  }
  int Integer(const std::string& path) {
    const double d = Number(path);
    if (d != std::floor(d)) Fail("\"" + path + "\" must be a whole number");
    return static_cast<int>(d);
  }
  // Degrees in the file, radians in the game. (deg / 180) * pi keeps -90
  // exactly -pi/2.
  double Angle(const std::string& path) { return Number(path) / 180.0 * kPi; }
  std::string String(const std::string& path) {
    const Value* v = At(path);
    if (v && !v->is_string()) Fail("\"" + path + "\" must be a string");
    return v && v->is_string() ? v->get<std::string>() : std::string();
  }
  void Cell(const std::string& path, double& x, double& y) {
    const Value* v = At(path);
    if (v && (!v->is_array() || v->size() != 2 || !(*v)[0].is_number() ||
              !(*v)[1].is_number())) {
      Fail("\"" + path + "\" must be [x, y]");
      return;
    }
    if (v) {
      x = (*v)[0].get<double>();
      y = (*v)[1].get<double>();
    }
  }
  void Cell(const std::string& path, int& x, int& y) {
    double dx = 0, dy = 0;
    Cell(path, dx, dy);
    x = static_cast<int>(dx);
    y = static_cast<int>(dy);
  }
  void Fail(const std::string& what) {
    if (error_.empty()) error_ = file_ + ": " + what;
  }
  bool Ok() const { return error_.empty(); }
  const std::string& Error() const { return error_; }

 private:
  const Value& root_;
  std::string file_;
  std::string error_;
};

}  // namespace mythos::json

#endif  // MYTHOS_ENGINE_CORE_JSON_READER_H_
