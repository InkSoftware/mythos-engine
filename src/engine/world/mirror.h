#ifndef MYTHOS_ENGINE_WORLD_MIRROR_H_
#define MYTHOS_ENGINE_WORLD_MIRROR_H_

#include <string>

#include "engine/core/json_reader.h"
#include "engine/world/interact.h"
#include "engine/world/level.h"

// A planar wall mirror made of a prop: where its glass stands and whether the
// player can see it. Pure arithmetic; the renderer takes the result as its
// mirror state.
namespace mythos {
// The mirror's prop and glass, from a game's tuning data (meters). The glass is
// a rectangle a little in front of the model's back; it shows while the player
// is within range with the glass in sight.
struct MirrorParams {
  // Id of the mirror's prop.
  std::string prop;
  // Half extents of the glass rectangle.
  double glass_half_width = 0.3, glass_half_height = 0.4;
  // Height of the glass center above the prop's base.
  double glass_height = 0.4;
  // How far the glass stands out from the prop's origin along its facing.
  double glass_depth = 0.03;
  // Farthest eye distance, in meters, at which the mirror is shown.
  double range = 9.0;
};

// Reads the object at path: every key (prop, glassHalfWidth, glassHalfHeight,
// glassHeight, glassDepth, range) is required; range and the glass half sizes
// must be positive. Returns r.Ok().
bool LoadMirrorParams(json::Reader& r, const std::string& path,
                      MirrorParams& out);

// Where the glass stands: its center, the horizontal unit normal facing into
// the room, and its half size. Meaningful only when on is true.
struct MirrorPlacement {
  bool on = false;
  double x = 0, y = 0, z = 0;
  double nx = 0, nz = 1;
  double half_w = 0, half_h = 0;
};

// Places the glass of prop as seen from the eye at (eye_x, eye_z). The result
// is on only if the prop has a model (one with none draws nothing), the glass
// is within range, and a clear line runs from the eye to a point in front of
// the glass.
MirrorPlacement PlaceMirror(const MirrorParams& params, const Prop& prop,
                            double eye_x, double eye_z, const Level& level);
}

#endif
