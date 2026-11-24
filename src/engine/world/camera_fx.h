#ifndef MYTHOS_ENGINE_WORLD_CAMERA_FX_H_
#define MYTHOS_ENGINE_WORLD_CAMERA_FX_H_

#include <cstdint>

// Camera feel: trauma-based shake and a field-of-view kick, as small offsets a
// game adds to its camera each frame. Deterministic (a seed, no clock), and
// the engine's renderer takes the result through `ApplyCameraFx`.
namespace mythos {
// The offsets for one frame. Angles are radians added to the view's yaw and
// pitch; `right` and `up` are metres in the camera's frame (`right` along the
// view's right, `up` along y); `roll` is radians of tilt about the view
// direction (positive drops the camera's right side, so the horizon turns
// counter-clockwise on the screen);
// `fov_scale` multiplies the field of view (1 = unchanged).
struct CameraFxOffsets {
  double yaw = 0;
  double pitch = 0;
  double right = 0;
  double up = 0;
  double roll = 0;
  double fov_scale = 1;
};

// How hard a full shake (trauma 1) moves the camera, and how it fades.
struct ShakeParams {
  double max_yaw = 0.05;
  double max_pitch = 0.04;
  double max_right = 0.04;
  double max_up = 0.03;
  // A shake can tilt the picture too: 0 (the default) keeps it level, as
  // before roll existed.
  double max_roll = 0.0;
  // How fast the noise moves, in cycles per second.
  double frequency = 14.0;
  // Trauma lost per second.
  double decay = 1.0;
  // The shake is trauma to this power, so small hits stay small.
  double power = 2.0;
  // How fast a field-of-view kick fades, per second (the offset is divided by
  // e each 1 / rate seconds).
  double fov_rate = 6.0;
  // The most a kick may widen or narrow the view (a fraction of it).
  double fov_limit = 0.3;
};

class CameraFx {
 public:
  explicit CameraFx(const uint32_t seed = 1) : seed_(seed) {}

  ShakeParams params;

  // Adds trauma, 0..1 (a footstep 0.05, a slam 0.4, an explosion 1); the total
  // stays within 0..1. Negative amounts are ignored.
  void AddShake(double trauma);

  // Pushes the field of view: positive widens it (a dash, a fall), negative
  // narrows it (a zoom), as a fraction of the view. It then eases back.
  void KickFov(double amount);

  // Moves the clock on by `dt` seconds; a negative `dt` is ignored.
  void Update(double dt);

  // 0..1 scale on everything this produces (an accessibility setting: 0 turns
  // the camera effects off). The default is 1.
  void SetIntensity(double intensity);

  // Stops everything at once.
  void Clear();

  [[nodiscard]] double Trauma() const { return trauma_; }

  // The offsets now. All zero (and `fov_scale` 1) when nothing is running or
  // the intensity is 0.
  [[nodiscard]] CameraFxOffsets Sample() const;

 private:
  uint32_t seed_;
  double trauma_ = 0;
  double fov_ = 0;
  double time_ = 0;
  double intensity_ = 1;
};

// The view's axes for a yaw, pitch and roll (radians; the player's convention:
// forward is (cos yaw cos pitch, sin pitch, sin yaw cos pitch), and a roll of
// 0 keeps `right` horizontal). `up` is perpendicular to both; a positive roll
// drops `right` (and tips `up` toward the old `right`).
struct ViewBasis {
  double forward[3], right[3], up[3];
};
ViewBasis MakeViewBasis(double yaw, double pitch, double roll = 0.0);
}  // namespace mythos

#endif
