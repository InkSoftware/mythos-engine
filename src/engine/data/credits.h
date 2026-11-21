// Credits page data.
#ifndef MYTHOS_ENGINE_DATA_CREDITS_H_
#define MYTHOS_ENGINE_DATA_CREDITS_H_

#include <string>
#include <vector>

#include "engine/core/json.h"

namespace mythos {
// One credited work or person: `name`, author `by`, `license` and `what` it was
// used for.
struct CreditEntry {
  std::string name, by, license;
  std::string what;
};

// A titled group of entries.
struct CreditSection {
  std::string title;
  std::vector<CreditEntry> entries;
};

// Reads `root["sections"]` into `out`. Returns false on the first malformed
// section or entry, with a message in `*error`.
bool LoadCredits(const json::Value& root, const std::string& file,
                 std::vector<CreditSection>& out, std::string* error);
}

#endif
