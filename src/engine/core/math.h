// Small math types and helpers shared across the engine.
#ifndef MYTHOS_ENGINE_CORE_MATH_H_
#define MYTHOS_ENGINE_CORE_MATH_H_

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mythos {
constexpr double kPi = std::numbers::pi;

struct Vec2 {
  double x = 0;
  double y = 0;
};

struct Vec3 {
  double x = 0;
  double y = 0;
  double z = 0;
};

// Distance along the unit-length `dir` from `origin` to the point nearest a
// sphere's center, if the ray passes within `radius` of it; -1 on a miss, when
// the center is behind the origin or farther than `max_t`. The nearest approach
// is returned, not the entry point, which is close enough for picking small
// objects.
inline double RayHitsSphere(const Vec3& origin, const Vec3& dir,
                            const Vec3& centre, const double radius,
                            const double max_t) {
  const double cx = centre.x - origin.x, cy = centre.y - origin.y,
               cz = centre.z - origin.z;
  const double t = cx * dir.x + cy * dir.y + cz * dir.z;
  if (t <= 0 || t > max_t) return -1;
  const double miss2 = cx * cx + cy * cy + cz * cz - t * t;
  return miss2 > radius * radius ? -1 : t;
}

// Clamps `v` to [lo, hi].
inline double Clampd(const double v, const double lo, const double hi) {
  return std::max(lo, std::min(hi, v));
}

// Signed difference a - b wrapped to [-pi, pi). The 3 * pi offset keeps the
// fmod operand positive for any inputs within a turn of each other.
inline double AngleDiff(const double a, const double b) {
  return std::fmod(a - b + 3 * kPi, 2 * kPi) - kPi;
}
}

#endif
