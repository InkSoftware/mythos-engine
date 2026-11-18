#include "engine/world/mirror.h"

#include <cmath>

#include "engine/core/math.h"

namespace mythos {
namespace {

// The eye must see a point this far in front of the glass (metres), or a wall hides the mirror.
constexpr double kClearanceInFront = 0.3;

}  // namespace

bool loadMirrorParams(json::Reader& r, const std::string& path, MirrorParams& out) {
    out.prop = r.string(path + ".prop");
    out.glassHalfWidth = r.number(path + ".glassHalfWidth");
    out.glassHalfHeight = r.number(path + ".glassHalfHeight");
    out.glassHeight = r.number(path + ".glassHeight");
    out.glassDepth = r.number(path + ".glassDepth");
    out.range = r.number(path + ".range");
    if (r.ok() && (out.range <= 0 || out.glassHalfWidth <= 0 || out.glassHalfHeight <= 0))
        r.fail(path + ".range and the glass size must be positive");
    return r.ok();
}

MirrorPlacement placeMirror(const MirrorParams& params, const Prop& prop, double eyeX, double eyeZ, const Level& level) {
    MirrorPlacement out;
    if (prop.model.empty()) return out;
    const double yaw = prop.yawDeg * kPi / 180.0;
    const double nx = std::sin(yaw), nz = std::cos(yaw);  // the way the model's front faces
    const double gx = prop.at.x + nx * params.glassDepth, gz = prop.at.y + nz * params.glassDepth;
    if (std::hypot(eyeX - gx, eyeZ - gz) > params.range) return out;
    if (!level.lineOfSight(eyeX, eyeZ, gx + nx * kClearanceInFront, gz + nz * kClearanceInFront)) return out;
    out.on = true;
    out.x = gx;
    out.z = gz;
    out.y = prop.lift + params.glassHeight;
    out.nx = nx;
    out.nz = nz;
    out.halfW = params.glassHalfWidth;
    out.halfH = params.glassHalfHeight;
    return out;
}

}  // namespace mythos
