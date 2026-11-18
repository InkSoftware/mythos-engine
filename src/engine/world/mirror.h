#pragma once
// A planar wall mirror made of a prop: where its glass stands and whether the player can see it. Pure arithmetic
// (no GL): the renderer's MirrorState takes the result (see setMirror in platform/gl/body_instances.h).
#include <string>

#include "engine/core/json_reader.h"
#include "engine/world/interact.h"
#include "engine/world/level.h"

namespace mythos {

// The mirror's prop and its glass, as a game's tuning data gives them (metres). The glass is a rectangle a little in
// front of the model's back; it shows while the player is within `range` with the glass in sight.
struct MirrorParams {
    std::string prop;                        // the id of the mirror's prop
    double glassHalfWidth = 0.3, glassHalfHeight = 0.4;
    double glassHeight = 0.4;                // the middle of the glass above the prop's base
    double glassDepth = 0.03;                // how far the glass stands out from the prop's back
    double range = 9.0;
};

// Read from the object at `path`: every key, all required; the range and the glass size must be positive.
bool loadMirrorParams(json::Reader& r, const std::string& path, MirrorParams& out);

// Where the glass stands: its centre, the horizontal unit normal facing into the room, and its half size.
struct MirrorPlacement {
    bool on = false;
    double x = 0, y = 0, z = 0;
    double nx = 0, nz = 1;
    double halfW = 0, halfH = 0;
};

// The mirror hung as `prop` is (a prop with no model draws nothing, so there is no mirror), seen from (eyeX, eyeZ):
// `on` only within the range and with nothing solid in between.
MirrorPlacement placeMirror(const MirrorParams& m, const Prop& prop, double eyeX, double eyeZ, const Level& level);

}  // namespace mythos
