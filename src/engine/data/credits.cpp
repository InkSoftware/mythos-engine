#include "engine/data/credits.h"

#include "engine/core/json_reader.h"

namespace mythos {

bool loadCredits(const json::Value& root, const std::string& file, std::vector<CreditSection>& out, std::string* error) {
    json::Reader reader(root, file);
    const json::Value* sections = reader.at("sections");
    if (sections && !sections->is_array()) reader.fail("\"sections\" must be an array");
    if (sections && sections->is_array()) {
        // Reads a required string member into `target`; records the first failure and returns false.
        auto readString = [&](const json::Value& object, const char* key, const std::string& where, std::string& target) {
            const json::Value* value = json::find(object, key);
            if (!value || !value->is_string()) {
                reader.fail(where + ": missing or bad \"" + key + "\"");
                return false;
            }
            target = value->get<std::string>();
            return true;
        };
        for (const auto& sectionJson : *sections) {
            CreditSection section;
            if (!readString(sectionJson, "title", "credits section", section.title)) break;
            const json::Value* entries = json::find(sectionJson, "entries");
            if (!entries || !entries->is_array()) {
                reader.fail("credits section \"" + section.title + "\": \"entries\" must be an array");
                break;
            }
            for (const auto& entryJson : *entries) {
                CreditEntry entry;
                const std::string where = "credits section \"" + section.title + "\", entry";
                if (!readString(entryJson, "name", where, entry.name) || !readString(entryJson, "by", where, entry.by) ||
                    !readString(entryJson, "license", where, entry.license) || !readString(entryJson, "what", where, entry.what))
                    break;
                if (entry.name.empty()) reader.fail(where + ": \"name\" must not be empty");
                section.entries.push_back(std::move(entry));
            }
            out.push_back(std::move(section));
        }
    }
    if (!reader.ok() && error) *error = reader.error();
    return reader.ok();
}

}  // namespace mythos
