#ifndef MYTHOS_ENGINE_DATA_I18N_H_
#define MYTHOS_ENGINE_DATA_I18N_H_
// Tiny key-based localisation: tr("menu.newGame") looks the key up in the
// current language's table (the game's, then the engine's own), falls back to
// English the same way, and finally returns the key itself (so a missing entry
// is visible on screen). The game supplies the tables with setTranslations().
// Strings are UTF-8; the bitmap font renders Greek.

#include <string>
#include <unordered_map>

namespace mythos {

enum class Lang { kEnglish, kGreek };

using TranslationTable = std::unordered_map<std::string, std::string>;

void SetLanguage(Lang l);

// Registers the table used for language l (the table must outlive its use; null
// clears it).
void SetTranslations(Lang l, const TranslationTable* table);

// The engine's own strings (the settings page, ON / OFF), used by tr() when the
// game's tables lack a key. See engine_strings.cpp.
const TranslationTable& EngineStrings(Lang l);

// Translates a text key ("hud.outside") into the current language.
std::string Tr(const std::string& key);

}  // namespace mythos

#endif  // MYTHOS_ENGINE_DATA_I18N_H_
