#ifndef MYTHOS_ENGINE_UI_SETTINGS_CVARS_H_
#define MYTHOS_ENGINE_UI_SETTINGS_CVARS_H_

#include "engine/core/cvar.h"
#include "engine/ui/settings.h"

namespace mythos {
// Registers one console variable per setting, named as in settings.txt
// (`ToKeyValues`), so a new setting is reachable from the console without
// further work. Setting one goes through `ApplyKeyValues` (clamped as the
// menu does); the game applies the settings again when `reg.Revision()`
// changes. `s` must outlive `reg`.
void RegisterSettingsCvars(CvarRegistry& reg, GameSettings& s);
}  // namespace mythos

#endif
