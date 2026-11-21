#include "engine/core/json.h"

#include <algorithm>
#include <set>
#include <vector>

#include "engine/core/vfs.h"

namespace mythos::json {
namespace {
int LineAt(const std::string& text, size_t pos) {
  pos = std::min(pos, text.size());
  return 1 + static_cast<int>(std::count(
                 text.begin(), text.begin() + static_cast<std::ptrdiff_t>(pos),
                 '\n'));
}
}

// Duplicates are found while parsing but reported only once parsing succeeds,
// so syntax errors take priority.
bool Parse(const std::string& text, Value& out, std::string* const error) {
  out = Value{};
  std::vector<std::set<std::string>> keys;
  std::string duplicate;
  const auto watch = [&](int, const Value::parse_event_t event,
                         const Value& parsed) {
    if (event == Value::parse_event_t::object_start) {
      keys.emplace_back();
    } else if (event == Value::parse_event_t::object_end) {
      keys.pop_back();
    } else if (event == Value::parse_event_t::key && duplicate.empty() &&
               !keys.back().insert(parsed.get<std::string>()).second) {
      duplicate = parsed.get<std::string>();
    }
    return true;
  };
  try {
    Value parsed = Value::parse(text, watch);
    if (duplicate.empty()) {
      out = std::move(parsed);
      return true;
    }
  } catch (const Value::parse_error& problem) {
    if (error) {
      *error = "line " + std::to_string(LineAt(text, problem.byte)) + ": " +
               problem.what();
    }
    return false;
  }
  if (error) {
    // The parser gives no position for a duplicate, so point at its last
    // textual occurrence.
    const size_t where = text.rfind(Quote(duplicate));
    *error =
        "line " +
        std::to_string(LineAt(text, where == std::string::npos ? 0 : where)) +
        ": duplicate key " + Quote(duplicate);
  }
  return false;
}

bool ParseFile(const std::string& path, Value& out, std::string* const error) {
  std::string text;
  if (!vfs::ReadFile(path, text)) {
    if (error) *error = "missing file " + path;
    return false;
  }
  std::string parse_error;
  if (!Parse(text, out, &parse_error)) {
    if (error) *error = path + ": " + parse_error;
    return false;
  }
  return true;
}

std::string Quote(const std::string& text) { return Value(text).dump(); }
}
