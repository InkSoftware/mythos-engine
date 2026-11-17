#include "engine/core/json.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "engine/core/vfs.h"

namespace mythos::json {

const Value* Value::find(const std::string& key) const {
    for (const auto& m : members_)
        if (m.first == key) return &m.second;
    return nullptr;
}

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    bool run(Value& out, std::string* error) {
        if (s_.compare(0, 3, "\xEF\xBB\xBF") == 0) pos_ = 3;  // a UTF-8 byte order mark
        skipSpace();
        if (!value(out, 0)) return fail(error);
        skipSpace();
        if (pos_ != s_.size()) {
            err_ = "unexpected text after the value";
            return fail(error);
        }
        return true;
    }

private:
    static constexpr int kMaxDepth = 64;

    bool fail(std::string* error) {
        if (error) {
            int line = 1;
            for (size_t i = 0; i < pos_ && i < s_.size(); ++i)
                if (s_[i] == '\n') ++line;
            *error = "line " + std::to_string(line) + ": " + err_;
        }
        return false;
    }

    bool bad(const std::string& what) {
        err_ = what;
        return false;
    }

    void skipSpace() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }

    bool literal(const char* word) {
        size_t n = 0;
        while (word[n]) ++n;
        if (s_.compare(pos_, n, word) != 0) return false;
        pos_ += n;
        return true;
    }

    bool value(Value& v, int depth) {
        if (depth > kMaxDepth) return bad("nested too deeply");
        if (pos_ >= s_.size()) return bad("unexpected end of input");
        const char c = s_[pos_];
        if (c == '{') return object(v, depth);
        if (c == '[') return array(v, depth);
        if (c == '"') {
            v.type_ = Value::Type::String;
            return string(v.string_);
        }
        if (c == '-' || (c >= '0' && c <= '9')) return number(v);
        if (literal("true")) {
            v.type_ = Value::Type::Bool;
            v.bool_ = true;
            return true;
        }
        if (literal("false")) {
            v.type_ = Value::Type::Bool;
            v.bool_ = false;
            return true;
        }
        if (literal("null")) {
            v.type_ = Value::Type::Null;
            return true;
        }
        return bad(std::string("unexpected character '") + c + "'");
    }

    bool object(Value& v, int depth) {
        v.type_ = Value::Type::Object;
        ++pos_;  // {
        skipSpace();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            skipSpace();
            if (pos_ >= s_.size() || s_[pos_] != '"') return bad("expected a \"key\"");
            std::string key;
            if (!string(key)) return false;
            if (v.find(key)) return bad("duplicate key \"" + key + "\"");
            skipSpace();
            if (pos_ >= s_.size() || s_[pos_] != ':') return bad("expected ':' after \"" + key + "\"");
            ++pos_;
            skipSpace();
            Value member;
            if (!value(member, depth + 1)) return false;
            v.members_.emplace_back(std::move(key), std::move(member));
            skipSpace();
            if (pos_ < s_.size() && s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < s_.size() && s_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return bad("expected ',' or '}' in object");
        }
    }

    bool array(Value& v, int depth) {
        v.type_ = Value::Type::Array;
        ++pos_;  // [
        skipSpace();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            skipSpace();
            Value item;
            if (!value(item, depth + 1)) return false;
            v.items_.push_back(std::move(item));
            skipSpace();
            if (pos_ < s_.size() && s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < s_.size() && s_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return bad("expected ',' or ']' in array");
        }
    }

    bool number(Value& v) {
        const size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        auto digits = [&] {
            const size_t from = pos_;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
            return pos_ > from;
        };
        if (pos_ < s_.size() && s_[pos_] == '0') {
            ++pos_;
        } else if (!digits()) {
            return bad("bad number");
        }
        if (pos_ < s_.size() && s_[pos_] == '.') {
            ++pos_;
            if (!digits()) return bad("bad number");
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            if (!digits()) return bad("bad number");
        }
        // strtod uses the C locale's decimal point; the program never changes the locale.
        const std::string text = s_.substr(start, pos_ - start);
        v.type_ = Value::Type::Number;
        v.number_ = std::strtod(text.c_str(), nullptr);
        return true;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(unsigned& out) {
        if (pos_ + 4 > s_.size()) return bad("bad \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else return bad("bad \\u escape");
        }
        return true;
    }

    bool string(std::string& out) {
        ++pos_;  // opening quote
        out.clear();
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return bad("control character in string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= s_.size()) break;
            const char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {  // high surrogate: a low one must follow
                        unsigned lo = 0;
                        if (s_.compare(pos_, 2, "\\u") != 0) return bad("unpaired surrogate");
                        pos_ += 2;
                        if (!hex4(lo)) return false;
                        if (lo < 0xDC00 || lo > 0xDFFF) return bad("unpaired surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return bad("unpaired surrogate");
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return bad(std::string("bad escape \\") + e);
            }
        }
        return bad("unterminated string");
    }

    const std::string& s_;
    size_t pos_ = 0;
    std::string err_;
};

bool parse(const std::string& text, Value& out, std::string* error) {
    out = Value{};
    return Parser(text).run(out, error);
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

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 15];
                } else {
                    out += ch;
                }
        }
    }
    return out + "\"";
}

}  // namespace mythos::json
