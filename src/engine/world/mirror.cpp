#include "engine/world/mirror.h"

#include <cmath>

#include "engine/core/math.h"

namespace mythos {

bool loadMirrorParams(json::Reader& r, const std::string& path, MirrorParams& t) {
    t.prop = r.string(path + ".prop");
    t.glassHalfWidth = r.number(path + ".glassHalfWidth");
    t.glassHalfHeight = r.number(path + ".glassHalfHeight");
    t.glassHeight = r.number(path + ".glassHeight");
    t.glassDepth = r.number(path + ".glassDepth");
    t.range = r.number(path + ".range");
    if (r.ok() && (t.range <= 0 || t.glassHalfWidth <= 0 || t.glassHalfHeight <= 0))
        r.fail(path + ".range and the glass size must be positive");
    return r.ok();
}

MirrorPlacement placeMirror(const MirrorParams& m, const Prop& prop, double eyeX, double eyeZ, const Level& level) {
    MirrorPlacement out;
    if (prop.model.empty()) return out;
    const double a = prop.yawDeg * kPi / 180.0;
    const double nx = std::sin(a), nz = std::cos(a);  // the way the model's front faces
    const double gx = prop.at.x + nx * m.glassDepth, gz = prop.at.y + nz * m.glassDepth;
    if (std::hypot(eyeX - gx, eyeZ - gz) > m.range) return out;
    if (!level.lineOfSight(eyeX, eyeZ, gx + nx * 0.3, gz + nz * 0.3)) return out;
    out.on = true;
    out.x = gx;
    out.z = gz;
    out.y = prop.lift + m.glassHeight;
    out.nx = nx;
    out.nz = nz;
    out.halfW = m.glassHalfWidth;
    out.halfH = m.glassHalfHeight;
    return out;
}

}  // namespace mythos
