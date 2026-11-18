#ifndef MYTHOS_ENGINE_DATA_CREDITS_H_
#define MYTHOS_ENGINE_DATA_CREDITS_H_
// The credits page as data: { "sections": [ { "title": <key>, "entries": [ {
// "name", "by", "license", "what": <key or ""> } ] } ] }. Names, authors and
// licences are proper names and are not translated; the section title and
// `what` are translation keys.
#include <string>
#include <vector>

#include "engine/core/json.h"

namespace mythos {

struct CreditEntry {
  std::string name, by, license;
  std::string what;  // a translation key ("" none)
};
struct CreditSection {
  std::string title;  // a translation key
  std::vector<CreditEntry> entries;
};

bool LoadCredits(const json::Value& root, const std::string& file,
                 std::vector<CreditSection>& out, std::string* error);

}  // namespace mythos

#endif  // MYTHOS_ENGINE_DATA_CREDITS_H_
