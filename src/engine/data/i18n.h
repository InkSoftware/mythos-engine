// Localization: one active language, translation tables registered by the game,
// and built-in engine strings as the last fallback. Global state, set up on the
// main thread at startup.
#ifndef MYTHOS_ENGINE_DATA_I18N_H_
#define MYTHOS_ENGINE_DATA_I18N_H_

#include <string>
#include <unordered_map>

namespace mythos {
// Supported languages.
enum class Lang { kEnglish, kGreek };

// Dotted key to text.
using TranslationTable = std::unordered_map<std::string, std::string>;

// Selects the language Tr() uses.
void SetLanguage(Lang l);

// Registers a game's table for `l`; the table must outlive its use. Null
// unregisters.
void SetTranslations(Lang l, const TranslationTable* table);

// The built-in strings (settings menu) for `l`.
const TranslationTable& EngineStrings(Lang l);

// Looks up `key` in the current language, then in English, trying the game's
// table before the built-in one each time. Returns `key` itself if nothing
// matches.
std::string Tr(const std::string& key);
}

#endif
