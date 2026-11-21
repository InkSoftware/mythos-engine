#include "engine/world/player.h"

#include <algorithm>
#include <cmath>

#include "engine/world/movement.h"

namespace mythos {
bool LoadPlayerParams(json::Reader& r, const std::string& path,
                      PlayerParams& out) {
  out.radius = r.Number(path + ".radius");
  out.walk_speed = r.Number(path + ".walkSpeed");
  out.run_speed = r.Number(path + ".runSpeed");
  out.turn_speed = r.Number(path + ".turnSpeed");
  out.stride_length = r.Number(path + ".strideLength");
  out.mouse_sensitivity = r.Number(path + ".mouseSensitivity");
  out.max_pitch = r.Number(path + ".maxPitch");
  return r.Ok();
}

void ApplyLook(Player& player, const MoveInput& in,
               const PlayerParams& params) {
  player.angle += in.mouse_dx * params.mouse_sensitivity;
  player.pitch = Clampd(player.pitch - in.mouse_dy * params.mouse_sensitivity,
                        -params.max_pitch, params.max_pitch);
}

std::optional<char> PlayerWalker::Update(const Level& level, Player& player,
                                         const double dt, const MoveInput& in,
                                         const PlayerParams& params) {
  const double turn = params.turn_speed * dt;
  if (in.turn_left) player.angle -= turn;
  if (in.turn_right) player.angle += turn;

  const double forward = (in.forward ? 1.0 : 0.0) - (in.back ? 1.0 : 0.0);
  const double side =
      (in.strafe_right ? 1.0 : 0.0) - (in.strafe_left ? 1.0 : 0.0);
  const double base = (in.run ? params.run_speed : params.walk_speed) * dt;
  Vec2 step{};
  if (forward == 0 && side == 0) {
    // No key: a stick moves him at a speed that follows how far it is pushed.
    const double h = std::hypot(in.analog_forward, in.analog_side);
    if (h <= 0.0) return std::nullopt;
    const double scale = std::min(1.0, h) / h;
    const Vec2 a = WalkStep(player.angle, in.analog_forward * scale, 0, base);
    const Vec2 b = WalkStep(player.angle, 0, in.analog_side * scale, base);
    step = {.x = a.x + b.x, .y = a.y + b.y};
  } else {
    step = WalkStep(player.angle, forward, side, base);
  }
  const Vec2 before = player.pos;
  MoveWithCollision(level, player.pos, step.x, step.y, params.radius);

  stride_accum_ += std::hypot(player.pos.x - before.x, player.pos.y - before.y);
  if (stride_accum_ < params.stride_length) return std::nullopt;
  stride_accum_ = 0;
  return level.At(static_cast<int>(std::floor(player.pos.x)),
                  static_cast<int>(std::floor(player.pos.y)));
}

int FindAimed(const Level& level, const Player& player, const double eye_height,
              const double reach, const std::vector<AimCandidate>& candidates) {
  const double cos_pitch = std::cos(player.pitch);
  const Vec3 dir{.x = std::cos(player.angle) * cos_pitch,
                 .y = std::sin(player.pitch),
                 .z = std::sin(player.angle) * cos_pitch};
  const double eye_x = player.pos.x, eye_z = player.pos.y;
  double nearest_hit = reach;
  int best = -1;
  for (size_t i = 0; i < candidates.size(); ++i) {
    const AimCandidate& candidate = candidates[i];
    if (!candidate.enabled) continue;
    const double hit = RayHitsSphere(
        {.x = eye_x, .y = eye_height, .z = eye_z}, dir,
        {.x = candidate.at.x, .y = candidate.height, .z = candidate.at.y},
        candidate.radius, nearest_hit);
    if (hit < 0) continue;

    const double to_x = candidate.at.x - eye_x, to_z = candidate.at.y - eye_z;
    const double flat_distance = std::hypot(to_x, to_z);
    // Back off half a meter (or all the way, when closer) so the target's own
    // wall or furniture cell does not hide it; a target standing on furniture
    // pushed against a wall (the letter on the oven) backs off further, to the
    // edge of the cell it is in.
    const double reach_start = std::min(0.5, flat_distance);
    const double reach_max = std::min(1.5, flat_distance);
    double reach_back = reach_start;
    for (int step = 1; reach_back < reach_max; ++step) {
      const double ex = candidate.at.x - to_x / flat_distance * reach_back;
      const double ez = candidate.at.y - to_z / flat_distance * reach_back;
      if (!level.BoxBlocked(ex - 0.01, ez - 0.01, ex + 0.01, ez + 0.01)) break;
      reach_back = reach_start + step * 0.1;
    }
    const double back_off =
        flat_distance > 1e-9 ? reach_back / flat_distance : 1.0;
    if (!level.LineOfSight(eye_x, eye_z, candidate.at.x - to_x * back_off,
                           candidate.at.y - to_z * back_off)) {
      continue;
    }
    nearest_hit = hit;
    best = static_cast<int>(i);
  }
  return best;
}
}
