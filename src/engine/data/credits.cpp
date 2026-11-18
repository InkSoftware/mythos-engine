#include "engine/data/credits.h"

#include "engine/core/json_reader.h"

namespace mythos {

bool LoadCredits(const json::Value& root, const std::string& file,
                 std::vector<CreditSection>& out, std::string* error) {
  json::Reader reader(root, file);
  const json::Value* sections = reader.At("sections");
  if (sections && !sections->is_array()) {
    reader.Fail("\"sections\" must be an array");
  }
  if (sections && sections->is_array()) {
    // Reads a required string member into `target`; records the first failure
    // and returns false.
    const auto read_string = [&](const json::Value& object, const char* key,
                                 const std::string& where,
                                 std::string& target) {
      const json::Value* value = json::Find(object, key);
      if (!value || !value->is_string()) {
        reader.Fail(where + ": missing or bad \"" + key + "\"");
        return false;
      }
      target = value->get<std::string>();
      return true;
    };
    for (const auto& section_json : *sections) {
      CreditSection section;
      if (!read_string(section_json, "title", "credits section",
                       section.title)) {
        break;
      }
      const json::Value* entries = json::Find(section_json, "entries");
      if (!entries || !entries->is_array()) {
        reader.Fail("credits section \"" + section.title +
                    "\": \"entries\" must be an array");
        break;
      }
      for (const auto& entry_json : *entries) {
        CreditEntry entry;
        const std::string where =
            "credits section \"" + section.title + "\", entry";
        if (!read_string(entry_json, "name", where, entry.name) ||
            !read_string(entry_json, "by", where, entry.by) ||
            !read_string(entry_json, "license", where, entry.license) ||
            !read_string(entry_json, "what", where, entry.what)) {
          break;
        }
        if (entry.name.empty()) {
          reader.Fail(where + ": \"name\" must not be empty");
        }
        section.entries.push_back(std::move(entry));
      }
      out.push_back(std::move(section));
    }
  }
  if (!reader.Ok() && error) *error = reader.Error();
  return reader.Ok();
}

}  // namespace mythos
