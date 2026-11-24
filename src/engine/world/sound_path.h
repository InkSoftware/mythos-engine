// What stands between a sound and the listener on the grid, for
// audio/spatial.h's occlusion.
#ifndef MYTHOS_ENGINE_WORLD_SOUND_PATH_H_
#define MYTHOS_ENGINE_WORLD_SOUND_PATH_H_

#include "engine/core/math.h"
#include "engine/world/level.h"

namespace mythos {
struct SoundBlockers {
  // Wall cells (solid, thin walls and windows included) the straight line
  // from the source to the listener passes through, each cell once.
  int walls = 0;
  // Closed door cells it passes through.
  int closed_doors = 0;
};

// Walks the segment `from` to `to` (world units, Vec2::y is z) and counts the
// cells it crosses. The cells of the end points are not counted: a listener
// in a doorway is not behind it.
SoundBlockers CountSoundBlockers(const Level& level, const Vec2& from,
                                 const Vec2& to);
}

#endif
