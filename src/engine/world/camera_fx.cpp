#include "engine/world/camera_fx.h"

#include <cmath>

#include "engine/core/math.h"
#include "engine/core/noise.h"

namespace mythos {
namespace {
// Smooth 1D noise in -1..1: value noise on integer lattice points with a
// smoothstep between them; `channel` picks an independent stream.
double Noise1(const uint32_t seed, const int channel, const double x) {
  const double cell = std::floor(x);
  const double f = x - cell;
  const int i = static_cast<int>(cell);
  const uint32_t s = seed + static_cast<uint32_t>(channel) * 0x9e3779b9u;
  const double a = Hnoise(i, 0, s);
  const double b = Hnoise(i + 1, 0, s);
  const double k = f * f * (3 - 2 * f);
  return (a + (b - a) * k) * 2 - 1;
}

// Keeps the clock small so the lattice index never overflows; only done when
// nothing is shaking, so the jump cannot be seen.
constexpr double kTimeWrap = 4096.0;
}  // namespace

void CameraFx::AddShake(const double trauma) {
  if (!(trauma > 0)) return;
  trauma_ = Clampd(trauma_ + trauma, 0.0, 1.0);
}

void CameraFx::KickFov(const double amount) {
  if (!std::isfinite(amount)) return;
  fov_ = Clampd(fov_ + amount, -params.fov_limit, params.fov_limit);
}

void CameraFx::Update(const double dt) {
  if (!(dt > 0)) return;
  trauma_ = Clampd(trauma_ - params.decay * dt, 0.0, 1.0);
  fov_ *= std::exp(-params.fov_rate * dt);
  if (std::fabs(fov_) < 1e-6) fov_ = 0;
  time_ += dt * params.frequency;
  if (trauma_ == 0 && time_ > kTimeWrap) time_ = std::fmod(time_, kTimeWrap);
}

void CameraFx::SetIntensity(const double intensity) {
  intensity_ = std::isfinite(intensity) ? Clampd(intensity, 0.0, 1.0) : 1.0;
}

void CameraFx::Clear() {
  trauma_ = 0;
  fov_ = 0;
}

CameraFxOffsets CameraFx::Sample() const {
  CameraFxOffsets out;
  const double shake = std::pow(trauma_, params.power) * intensity_;
  if (shake > 0) {
    out.yaw = params.max_yaw * shake * Noise1(seed_, 0, time_);
    out.pitch = params.max_pitch * shake * Noise1(seed_, 1, time_);
    out.right = params.max_right * shake * Noise1(seed_, 2, time_);
    out.up = params.max_up * shake * Noise1(seed_, 3, time_);
  }
  out.fov_scale = 1.0 + fov_ * intensity_;
  return out;
}
}  // namespace mythos
