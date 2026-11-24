// JSON for the engine's own pieces of a save (see data/save.h): what the
// player has found and where they stand. Games add their own sections.
#ifndef MYTHOS_ENGINE_WORLD_SAVE_STATE_H_
#define MYTHOS_ENGINE_WORLD_SAVE_STATE_H_

#include "engine/core/json.h"
#include "engine/world/interact.h"
#include "engine/world/player.h"

namespace mythos {
json::Value ProgressToJson(const Progress& progress);
// Missing or mistyped members keep their defaults. Returns false if `v` is
// not an object (and leaves `out` alone).
bool ProgressFromJson(const json::Value& v, Progress& out);

json::Value PlayerToJson(const Player& player);
bool PlayerFromJson(const json::Value& v, Player& out);
}

#endif
