// Loading translation files into TranslationTables.
#ifndef MYTHOS_ENGINE_DATA_TRANSLATION_H_
#define MYTHOS_ENGINE_DATA_TRANSLATION_H_

#include <string>

#include "engine/core/json.h"
#include "engine/data/i18n.h"

namespace mythos {
// A loaded language file: its code, display name and flattened strings.
struct Translation {
  std::string language;
  std::string name;
  TranslationTable strings;
};

// Reads a translation JSON (`language`, `name`, nested `strings`) into `out`.
// Nested objects become dotted keys; keys starting with an underscore are
// skipped. Returns false on bad data or a duplicate key, with a message in
// `*error`.
bool LoadTranslation(const json::Value& root, const std::string& file,
                     Translation& out, std::string* error);

// Registers both tables with SetTranslations; the arguments must outlive their
// use.
void InstallTranslations(const Translation& english, const Translation& greek);
}

#endif
