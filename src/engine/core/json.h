#ifndef MYTHOS_ENGINE_CORE_JSON_H_
#define MYTHOS_ENGINE_CORE_JSON_H_
// JSON through nlohmann/json: Value is nlohmann::ordered_json (objects keep
// their key order, numbers read as doubles, strings are UTF-8), plus the strict
// loader every data file goes through.

#include <string>

#include <nlohmann/json.hpp>

namespace mythos::json {

using Value = nlohmann::ordered_json;

// Parses text (RFC 8259, a UTF-8 byte order mark is skipped). Unlike nlohmann's
// default, a duplicate key in an object is an error. On failure returns false
// and sets *error to "line L: what went wrong".
bool Parse(const std::string& text, Value& out, std::string* error = nullptr);

// Reads and parses a file through the vfs; errors are prefixed with the path.
bool ParseFile(const std::string& path, Value& out,
               std::string* error = nullptr);

// The member of an object, or nullptr (also when v is not an object).
inline const Value* Find(const Value& v, const std::string& key) {
  if (!v.is_object()) return nullptr;
  const auto it = v.find(key);
  return it == v.end() ? nullptr : &*it;
}

// A JSON string literal for s, with quotes and escapes ("a\"b"). UTF-8 passes
// through.
std::string Quote(const std::string& s);

}  // namespace mythos::json

#endif  // MYTHOS_ENGINE_CORE_JSON_H_
