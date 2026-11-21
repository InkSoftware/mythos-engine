// JSON support: the engine's document type and strict parsing helpers.
#ifndef MYTHOS_ENGINE_CORE_JSON_H_
#define MYTHOS_ENGINE_CORE_JSON_H_

#include <string>

#include <nlohmann/json.hpp>

namespace mythos::json {
// Insertion-ordered so saved files keep their key order.
using Value = nlohmann::ordered_json;

// Parses `text` into `out`; unlike the library default, duplicate object keys
// are an error. On failure returns false, leaves `out` null and stores a
// message with a line number in `*error`.
bool Parse(const std::string& text, Value& out, std::string* error = nullptr);

// Reads `path` through the vfs (so packed assets work) and parses it. The
// message in `*error` names the file.
bool ParseFile(const std::string& path, Value& out,
               std::string* error = nullptr);

// Returns the member `key` of an object, or nullptr when `v` is not an object
// or lacks it.
inline const Value* Find(const Value& v, const std::string& key) {
  if (!v.is_object()) return nullptr;
  const auto it = v.find(key);
  return it == v.end() ? nullptr : &*it;
}

// Returns `s` as a quoted, escaped JSON string.
std::string Quote(const std::string& s);
}

#endif
