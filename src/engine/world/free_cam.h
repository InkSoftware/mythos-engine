#ifndef MYTHOS_ENGINE_WORLD_FREE_CAM_H_
#define MYTHOS_ENGINE_WORLD_FREE_CAM_H_

#include "engine/world/player.h"

namespace mythos {
// A fly camera for debugging: looks with the mouse and moves along the view
// direction (strafe sideways, rise straight up), through walls. Pure
// arithmetic; the renderer takes the pose through `ApplyFreeCam`.
struct FreeCamera {
  // World position; y is height above the floor.
  double x = 0, y = 1.55, z = 0;
  // Yaw and pitch in radians, as for `Player` (pitch up is positive).
  double yaw = 0, pitch = 0;
  // Metres per second, and the multiplier while `MoveInput::run` is held.
  double speed = 4.0;
  double fast = 4.0;
  // Pitch is clamped to +-max_pitch.
  double max_pitch = 1.5;

  // Starts where the player is looking from.
  void Place(const Player& p, double eye_height);

  // Looks (mouse delta in `in`, scaled by `mouse_sensitivity` radians per
  // pixel) and flies for `dt` seconds. `rise` is -1..1 (down, up). Forward is
  // `in.forward` / `in.back` (or the analog stick when no key is held).
  void Update(const MoveInput& in, double rise, double dt,
              double mouse_sensitivity);
};
}  // namespace mythos

#endif
