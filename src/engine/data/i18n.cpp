#include "engine/data/i18n.h"

namespace mythos {
namespace {

Lang g_lang = Lang::English;
const TranslationTable* g_tables[2] = {nullptr, nullptr};

// The game's table first, then the engine's built-in strings; null when neither has the key.
const std::string* lookup(Lang lang, const std::string& key) {
    if (const TranslationTable* table = g_tables[static_cast<int>(lang)]) {
        const auto found = table->find(key);
        if (found != table->end()) return &found->second;
    }
    const TranslationTable& builtin = engineStrings(lang);
    const auto found = builtin.find(key);
    return found != builtin.end() ? &found->second : nullptr;
}

}  // namespace

void setLanguage(Lang l) { g_lang = l; }
Lang language() { return g_lang; }

void setTranslations(Lang l, const TranslationTable* table) { g_tables[static_cast<int>(l)] = table; }

std::string tr(const std::string& key) {
    if (const std::string* text = lookup(g_lang, key)) return *text;
    if (g_lang != Lang::English)  // fall back to English, then to the key itself
        if (const std::string* text = lookup(Lang::English, key)) return *text;
    return key;
}

}  // namespace mythos
