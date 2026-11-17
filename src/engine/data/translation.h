#pragma once
// A language file: { "language": "en", "name": "ENGLISH", "strings": { "hud": { "outside": "..." } } }.
// Nested sections flatten to dotted keys ("hud.outside"); "_" keys are comments.
#include <string>

#include "engine/data/i18n.h"
#include "engine/core/json.h"

namespace mythos {

struct Translation {
    std::string language;  // "en", "el"
    std::string name;      // the language's own name, shown on the settings page
    TranslationTable strings;
};

bool loadTranslation(const json::Value& root, const std::string& file, Translation& out, std::string* error);

// Makes tr() use these two (they must outlive its use).
void installTranslations(const Translation& english, const Translation& greek);

}  // namespace mythos
