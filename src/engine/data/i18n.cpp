#include "engine/data/i18n.h"

namespace mythos {
namespace {
Lang g_lang = Lang::kEnglish;
const TranslationTable* g_tables[2] = {nullptr, nullptr};

// Game table first, so a game can override engine strings.
const std::string* Lookup(const Lang lang, const std::string& key) {
  if (const TranslationTable* table = g_tables[static_cast<int>(lang)]) {
    const auto found = table->find(key);
    if (found != table->end()) return &found->second;
  }
  const TranslationTable& builtin = EngineStrings(lang);
  const auto found = builtin.find(key);
  return found != builtin.end() ? &found->second : nullptr;
}
}

void SetLanguage(const Lang l) { g_lang = l; }

void SetTranslations(const Lang l, const TranslationTable* const table) {
  g_tables[static_cast<int>(l)] = table;
}

std::string Tr(const std::string& key) {
  if (const std::string* text = Lookup(g_lang, key)) return *text;
  if (g_lang != Lang::kEnglish) {
    if (const std::string* text = Lookup(Lang::kEnglish, key)) return *text;
  }
  return key;
}
}
