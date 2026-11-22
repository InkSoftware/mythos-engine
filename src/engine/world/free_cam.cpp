#include "engine/world/free_cam.h"

#include <cmath>

namespace mythos {
void FreeCamera::Place(const Player& p, const double eye_height) {
  x = p.pos.x;
  z = p.pos.y;
  y = eye_height;
  yaw = p.angle;
  pitch = p.pitch;
}

void FreeCamera::Update(const MoveInput& in, const double rise, const double dt,
                        const double mouse_sensitivity) {
  yaw += in.mouse_dx * mouse_sensitivity;
  pitch =
      Clampd(pitch - in.mouse_dy * mouse_sensitivity, -max_pitch, max_pitch);
  if (!(dt > 0) || !std::isfinite(dt)) return;
  double fwd = (in.forward ? 1.0 : 0.0) - (in.back ? 1.0 : 0.0);
  double side = (in.strafe_right ? 1.0 : 0.0) - (in.strafe_left ? 1.0 : 0.0);
  if (fwd == 0.0 && side == 0.0) {
    fwd = Clampd(in.analog_forward, -1.0, 1.0);
    side = Clampd(in.analog_side, -1.0, 1.0);
  }
  const double up = Clampd(rise, -1.0, 1.0);
  const double step = speed * (in.run ? fast : 1.0) * dt;
  // The view direction and its right-hand side, as the renderer builds them.
  const double cp = std::cos(pitch), sp = std::sin(pitch);
  const double cy = std::cos(yaw), sy = std::sin(yaw);
  x += (cy * cp * fwd - sy * side) * step;
  y += (sp * fwd + up) * step;
  z += (sy * cp * fwd + cy * side) * step;
}
}  // namespace mythos
