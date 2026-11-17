#include "engine/data/i18n.h"

namespace mythos {
namespace {

Lang g_lang = Lang::English;
const TranslationTable* g_tables[2] = {nullptr, nullptr};

const std::string* lookup(Lang l, const std::string& key) {
    if (const TranslationTable* t = g_tables[static_cast<int>(l)]) {
        const auto it = t->find(key);
        if (it != t->end()) return &it->second;
    }
    const TranslationTable& builtin = engineStrings(l);
    const auto it = builtin.find(key);
    return it != builtin.end() ? &it->second : nullptr;
}

}  // namespace

void setLanguage(Lang l) { g_lang = l; }
Lang language() { return g_lang; }

void setTranslations(Lang l, const TranslationTable* table) { g_tables[static_cast<int>(l)] = table; }

std::string tr(const std::string& key) {
    if (const std::string* s = lookup(g_lang, key)) return *s;
    if (g_lang != Lang::English)
        if (const std::string* s = lookup(Lang::English, key)) return *s;
    return key;
}

}  // namespace mythos
