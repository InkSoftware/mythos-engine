// JSON for the engine's own pieces of a save (see data/save.h): what the
// player has found and where they stand. Games add their own sections.
#ifndef MYTHOS_ENGINE_WORLD_SAVE_STATE_H_
#define MYTHOS_ENGINE_WORLD_SAVE_STATE_H_

#include "engine/core/json.h"
#include "engine/world/interact.h"
#include "engine/world/level.h"
#include "engine/world/player.h"

namespace mythos {
json::Value ProgressToJson(const Progress& progress);
// Missing or mistyped members keep their defaults. Returns false if `v` is
// not an object (and leaves `out` alone).
bool ProgressFromJson(const json::Value& v, Progress& out);

// What changed in a level's map since `baseline` (the level as loaded): an
// array of `[x, y, "glyph"]` for each cell whose glyph differs. Doors that were
// opened and closed show up here, since the door system swaps their glyphs.
// The levels must have the same size; cells outside the smaller one are
// ignored.
json::Value LevelDiffToJson(const Level& baseline, const Level& now);

// Writes the cells of a diff into `level` (out of range cells and malformed
// entries are skipped). Returns how many cells were set. A game calls
// `RoomDoors::Scan` afterwards when doors may have changed.
int ApplyLevelDiff(Level& level, const json::Value& diff);

json::Value PlayerToJson(const Player& player);
bool PlayerFromJson(const json::Value& v, Player& out);
}

#endif
