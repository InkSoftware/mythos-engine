#ifndef MYTHOS_ENGINE_CORE_MATH_H_
#define MYTHOS_ENGINE_CORE_MATH_H_
// Small math helpers shared by the engine and the game.

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mythos {

constexpr double kPi = std::numbers::pi;

// A point or direction on the map plane (x = column, y = row).
struct Vec2 {
  double x = 0;
  double y = 0;
};

struct Vec3 {
  double x = 0;
  double y = 0;
  double z = 0;
};

// Where a ray (a unit direction dir from origin) first passes within `radius`
// of `centre`: the distance along the ray to the point nearest the centre, or
// -1 if the ray misses, the centre is behind it, or the point is farther than
// maxT.
inline double RayHitsSphere(const Vec3& origin, const Vec3& dir,
                            const Vec3& centre, double radius, double max_t) {
  const double cx = centre.x - origin.x, cy = centre.y - origin.y,
               cz = centre.z - origin.z;
  const double t = cx * dir.x + cy * dir.y + cz * dir.z;
  if (t <= 0 || t > max_t) return -1;
  const double miss2 = cx * cx + cy * cy + cz * cz - t * t;
  return miss2 > radius * radius ? -1 : t;
}

inline double Clampd(double v, double lo, double hi) {
  return std::max(lo, std::min(hi, v));
}

// Signed smallest difference between two angles, in [-pi, pi).
inline double AngleDiff(double a, double b) {
  return std::fmod(a - b + 3 * kPi, 2 * kPi) - kPi;
}

}  // namespace mythos

#endif  // MYTHOS_ENGINE_CORE_MATH_H_
