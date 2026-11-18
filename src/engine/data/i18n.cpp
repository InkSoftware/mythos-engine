#include "engine/data/i18n.h"

namespace mythos {
namespace {

Lang g_lang = Lang::kEnglish;
const TranslationTable* g_tables[2] = {nullptr, nullptr};

// The game's table first, then the engine's built-in strings; null when neither
// has the key.
const std::string* Lookup(Lang lang, const std::string& key) {
  if (const TranslationTable* table = g_tables[static_cast<int>(lang)]) {
    const auto found = table->find(key);
    if (found != table->end()) return &found->second;
  }
  const TranslationTable& builtin = EngineStrings(lang);
  const auto found = builtin.find(key);
  return found != builtin.end() ? &found->second : nullptr;
}

}  // namespace

void SetLanguage(Lang l) { g_lang = l; }
Lang Language() { return g_lang; }

void SetTranslations(Lang l, const TranslationTable* table) {
  g_tables[static_cast<int>(l)] = table;
}

std::string Tr(const std::string& key) {
  if (const std::string* text = Lookup(g_lang, key)) return *text;
  if (g_lang !=
      Lang::kEnglish) {  // fall back to English, then to the key itself
    if (const std::string* text = Lookup(Lang::kEnglish, key)) return *text;
  }
  return key;
}

}  // namespace mythos
