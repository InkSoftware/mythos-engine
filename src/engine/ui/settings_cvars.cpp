#include "engine/ui/settings_cvars.h"

#include <map>
#include <string>

namespace mythos {
void RegisterSettingsCvars(CvarRegistry& reg, GameSettings& s) {
  for (const auto& entry : ToKeyValues(s)) {
    const std::string key = entry.first;
    reg.AddCustom(
        key, [&s, key] { return ToKeyValues(s)[key]; },
        [&s, key](const std::string& value) -> CvarResult {
          ApplyKeyValues(s, {{key, value}});
          return {.ok = true, .text = ""};
        },
        "setting (settings.txt key)");
  }
}
}  // namespace mythos
