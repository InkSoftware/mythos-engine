#include "engine/data/credits.h"

#include "engine/core/json_reader.h"

namespace mythos {

bool loadCredits(const json::Value& root, const std::string& file, std::vector<CreditSection>& out, std::string* error) {
    json::Reader r(root, file);
    const json::Value* all = r.at("sections");
    if (all && !all->isArray()) r.fail("\"sections\" must be an array");
    if (all && all->isArray()) {
        auto text = [&](const json::Value& obj, const char* key, const std::string& where, std::string& dst) {
            const json::Value* v = obj.isObject() ? obj.find(key) : nullptr;
            if (!v || !v->isString()) {
                r.fail(where + ": missing or bad \"" + key + "\"");
                return false;
            }
            dst = v->asString();
            return true;
        };
        for (const auto& sv : all->items()) {
            CreditSection sec;
            if (!text(sv, "title", "credits section", sec.title)) break;
            const json::Value* entries = sv.find("entries");
            if (!entries || !entries->isArray()) {
                r.fail("credits section \"" + sec.title + "\": \"entries\" must be an array");
                break;
            }
            for (const auto& ev : entries->items()) {
                CreditEntry e;
                const std::string where = "credits section \"" + sec.title + "\", entry";
                if (!text(ev, "name", where, e.name) || !text(ev, "by", where, e.by) ||
                    !text(ev, "license", where, e.license) || !text(ev, "what", where, e.what))
                    break;
                if (e.name.empty()) r.fail(where + ": \"name\" must not be empty");
                sec.entries.push_back(std::move(e));
            }
            out.push_back(std::move(sec));
        }
    }
    if (!r.ok() && error) *error = r.error();
    return r.ok();
}

}  // namespace mythos
