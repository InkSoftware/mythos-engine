#include "engine/core/json.h"

#include <algorithm>
#include <set>
#include <vector>

#include "engine/core/vfs.h"

namespace mythos::json {
namespace {

int lineAt(const std::string& text, size_t pos) {
    pos = std::min(pos, text.size());
    return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(pos), '\n'));
}

}  // namespace

bool parse(const std::string& text, Value& out, std::string* error) {
    out = Value{};
    std::vector<std::set<std::string>> keys;  // the keys seen so far in each open object
    std::string duplicate;
    const auto watch = [&](int, Value::parse_event_t event, Value& parsed) {
        if (event == Value::parse_event_t::object_start) {
            keys.emplace_back();
        } else if (event == Value::parse_event_t::object_end) {
            keys.pop_back();
        } else if (event == Value::parse_event_t::key && duplicate.empty() && !keys.back().insert(parsed.get<std::string>()).second) {
            duplicate = parsed.get<std::string>();
        }
        return true;
    };
    try {
        Value v = Value::parse(text, watch);
        if (duplicate.empty()) {
            out = std::move(v);
            return true;
        }
    } catch (const Value::parse_error& e) {
        if (error) *error = "line " + std::to_string(lineAt(text, e.byte)) + ": " + e.what();
        return false;
    }
    if (error) {
        // nlohmann does not say where; the duplicate is the key's last appearance in the text.
        const size_t at = text.rfind(quote(duplicate));
        *error = "line " + std::to_string(lineAt(text, at == std::string::npos ? 0 : at)) + ": duplicate key " + quote(duplicate);
    }
    return false;
}

bool parseFile(const std::string& path, Value& out, std::string* error) {
    std::string text;
    if (!vfs::readFile(path, text)) {
        if (error) *error = "missing file " + path;
        return false;
    }
    std::string err;
    if (!parse(text, out, &err)) {
        if (error) *error = path + ": " + err;
        return false;
    }
    return true;
}

std::string quote(const std::string& s) { return Value(s).dump(); }

}  // namespace mythos::json
