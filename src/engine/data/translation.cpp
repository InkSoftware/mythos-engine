#include "engine/data/translation.h"

#include "engine/core/json_reader.h"

namespace mythos {
namespace {

// Flattens nested sections to dotted keys: { "hud": { "house": "..." } } -> "hud.house".
bool flatten(const json::Value& section, const std::string& prefix, TranslationTable& out, std::string& error) {
    for (const auto& [key, value] : section.items()) {
        if (!key.empty() && key[0] == '_') continue;
        const std::string path = prefix.empty() ? key : prefix + "." + key;
        if (value.is_object()) {
            if (!flatten(value, path, out, error)) return false;
        } else if (value.is_string()) {
            if (!out.emplace(path, value.get<std::string>()).second) {
                error = "\"" + path + "\" is defined twice";
                return false;
            }
        } else {
            error = "\"" + path + "\" must be a string or a section object";
            return false;
        }
    }
    return true;
}

}  // namespace

bool loadTranslation(const json::Value& root, const std::string& file, Translation& out, std::string* error) {
    json::Reader reader(root, file);
    out.language = reader.string("language");
    out.name = reader.string("name");
    if (const json::Value* strings = reader.at("strings")) {
        std::string flattenError;
        if (!strings->is_object() || !flatten(*strings, "", out.strings, flattenError))
            reader.fail(flattenError.empty() ? "\"strings\" must be an object" : flattenError);
    }
    if (!reader.ok() && error) *error = reader.error();
    return reader.ok();
}

void installTranslations(const Translation& english, const Translation& greek) {
    setTranslations(Lang::English, &english.strings);
    setTranslations(Lang::Greek, &greek.strings);
}

}  // namespace mythos
