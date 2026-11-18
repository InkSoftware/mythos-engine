#include "engine/data/translation.h"

#include "engine/core/json_reader.h"

namespace mythos {
namespace {

// Flattens nested sections to dotted keys: { "hud": { "house": "..." } } ->
// "hud.house".
bool Flatten(const json::Value& section, const std::string& prefix,
             TranslationTable& out, std::string& error) {
  for (const auto& [key, value] : section.items()) {
    if (!key.empty() && key[0] == '_') continue;
    const std::string path = prefix.empty() ? key : prefix + "." + key;
    if (value.is_object()) {
      if (!Flatten(value, path, out, error)) return false;
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

bool LoadTranslation(const json::Value& root, const std::string& file,
                     Translation& out, std::string* error) {
  json::Reader reader(root, file);
  out.language = reader.String("language");
  out.name = reader.String("name");
  if (const json::Value* strings = reader.At("strings")) {
    std::string flatten_error;
    if (!strings->is_object() ||
        !Flatten(*strings, "", out.strings, flatten_error)) {
      reader.Fail(flatten_error.empty() ? "\"strings\" must be an object"
                                        : flatten_error);
    }
  }
  if (!reader.Ok() && error) *error = reader.Error();
  return reader.Ok();
}

void InstallTranslations(const Translation& english, const Translation& greek) {
  SetTranslations(Lang::kEnglish, &english.strings);
  SetTranslations(Lang::kGreek, &greek.strings);
}

}  // namespace mythos
