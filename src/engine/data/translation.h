#ifndef MYTHOS_ENGINE_DATA_TRANSLATION_H_
#define MYTHOS_ENGINE_DATA_TRANSLATION_H_
// A language file: { "language": "en", "name": "ENGLISH", "strings": { "hud": {
// "outside": "..." } } }. Nested sections flatten to dotted keys
// ("hud.outside"); "_" keys are comments.
#include <string>

#include "engine/core/json.h"
#include "engine/data/i18n.h"

namespace mythos {

struct Translation {
  std::string language;  // "en", "el"
  std::string name;      // the language's own name, shown on the settings page
  TranslationTable strings;
};

bool LoadTranslation(const json::Value& root, const std::string& file,
                     Translation& out, std::string* error);

// Makes tr() use these two (they must outlive its use).
void InstallTranslations(const Translation& english, const Translation& greek);

}  // namespace mythos

#endif  // MYTHOS_ENGINE_DATA_TRANSLATION_H_
