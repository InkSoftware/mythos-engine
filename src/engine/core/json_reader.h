// A reader for config files that finds values by dotted path and records the
// first error instead of throwing, so a loader can read everything and check
// Ok() once.
#ifndef MYTHOS_ENGINE_CORE_JSON_READER_H_
#define MYTHOS_ENGINE_CORE_JSON_READER_H_

#include <cmath>
#include <string>
#include <utility>

#include "engine/core/json.h"
#include "engine/core/math.h"

namespace mythos::json {
// Accessors return a neutral value (0, empty string) after an error. Only the
// first error is kept, prefixed with the file name. Holds a reference to the
// root, which must outlive the reader.
class Reader {
 public:
  // `file` is used only to prefix error messages.
  Reader(const Value& root, std::string file)
      : root_(root), file_(std::move(file)) {}

  // Follows a dotted path such as "player.speed". Returns nullptr and records
  // an error if any part is missing.
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

  // Number at `path`; an error if missing or not a number.
  double Number(const std::string& path) {
    const Value* v = At(path);
    if (v && !v->is_number()) Fail("\"" + path + "\" must be a number");
    return v && v->is_number() ? v->get<double>() : 0.0;
  }

  // Number at `path` that must be whole.
  int Integer(const std::string& path) {
    const double d = Number(path);
    if (d != std::floor(d)) Fail("\"" + path + "\" must be a whole number");
    return static_cast<int>(d);
  }

  // Number at `path` given in degrees, returned in radians.
  double Angle(const std::string& path) { return Number(path) / 180.0 * kPi; }

  // String at `path`.
  std::string String(const std::string& path) {
    const Value* v = At(path);
    if (v && !v->is_string()) Fail("\"" + path + "\" must be a string");
    return v && v->is_string() ? v->get<std::string>() : std::string();
  }

  // Reads a two-number array [x, y] into the outputs, which are left unchanged
  // on error.
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

  // Integer overload; fractions are truncated.
  void Cell(const std::string& path, int& x, int& y) {
    double dx = 0, dy = 0;
    Cell(path, dx, dy);
    x = static_cast<int>(dx);
    y = static_cast<int>(dy);
  }

  // Records an error unless one is already recorded.
  void Fail(const std::string& what) {
    if (error_.empty()) error_ = file_ + ": " + what;
  }

  [[nodiscard]] bool Ok() const { return error_.empty(); }
  [[nodiscard]] const std::string& Error() const { return error_; }

 private:
  const Value& root_;
  std::string file_;
  std::string error_;
};
}

#endif
