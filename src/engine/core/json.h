#pragma once
// A small JSON reader (RFC 8259): objects keep their key order, numbers are doubles,
// strings are UTF-8 (\u escapes, surrogate pairs included). No dependencies. Writing is left
// to the caller, with quote() for strings.

#include <string>
#include <utility>
#include <vector>

namespace mythos::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool() const { return bool_; }
    double asNumber() const { return number_; }
    const std::string& asString() const { return string_; }
    const std::vector<Value>& items() const { return items_; }                         // array
    const std::vector<std::pair<std::string, Value>>& members() const { return members_; }  // object

    // Object member by key, or nullptr (also when this is not an object).
    const Value* find(const std::string& key) const;

private:
    friend class Parser;
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0;
    std::string string_;
    std::vector<Value> items_;
    std::vector<std::pair<std::string, Value>> members_;
};

// Parses text. On failure returns false and sets *error to "line L: what went wrong".
bool parse(const std::string& text, Value& out, std::string* error = nullptr);

// Reads and parses a file; errors are prefixed with the path.
bool parseFile(const std::string& path, Value& out, std::string* error = nullptr);

// A JSON string literal for s, with quotes and escapes ("a\"b"). UTF-8 passes through.
std::string quote(const std::string& s);

}  // namespace mythos::json
