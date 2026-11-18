#include "engine/world/mirror.h"

#include <cmath>

#include "engine/core/math.h"

namespace mythos {
namespace {

// The eye must see a point this far in front of the glass (metres), or a wall
// hides the mirror.
constexpr double kClearanceInFront = 0.3;

}  // namespace

bool LoadMirrorParams(json::Reader& r, const std::string& path,
                      MirrorParams& out) {
  out.prop = r.String(path + ".prop");
  out.glass_half_width = r.Number(path + ".glassHalfWidth");
  out.glass_half_height = r.Number(path + ".glassHalfHeight");
  out.glass_height = r.Number(path + ".glassHeight");
  out.glass_depth = r.Number(path + ".glassDepth");
  out.range = r.Number(path + ".range");
  if (r.Ok() && (out.range <= 0 || out.glass_half_width <= 0 ||
                 out.glass_half_height <= 0)) {
    r.Fail(path + ".range and the glass size must be positive");
  }
  return r.Ok();
}

MirrorPlacement PlaceMirror(const MirrorParams& params, const Prop& prop,
                            double eye_x, double eye_z, const Level& level) {
  MirrorPlacement out;
  if (prop.model.empty()) return out;
  const double yaw = prop.yaw_deg * kPi / 180.0;
  const double nx = std::sin(yaw),
               nz = std::cos(yaw);  // the way the model's front faces
  const double gx = prop.at.x + nx * params.glass_depth,
               gz = prop.at.y + nz * params.glass_depth;
  if (std::hypot(eye_x - gx, eye_z - gz) > params.range) return out;
  if (!level.LineOfSight(eye_x, eye_z, gx + nx * kClearanceInFront,
                         gz + nz * kClearanceInFront)) {
    return out;
  }
  out.on = true;
  out.x = gx;
  out.z = gz;
  out.y = prop.lift + params.glass_height;
  out.nx = nx;
  out.nz = nz;
  out.half_w = params.glass_half_width;
  out.half_h = params.glass_half_height;
  return out;
}

}  // namespace mythos
