#include "engine/world/player.h"

#include <cmath>

#include "engine/world/movement.h"

namespace mythos {

bool loadPlayerParams(json::Reader& r, const std::string& path, PlayerParams& out) {
    out.radius = r.number(path + ".radius");
    out.walkSpeed = r.number(path + ".walkSpeed");
    out.runSpeed = r.number(path + ".runSpeed");
    out.turnSpeed = r.number(path + ".turnSpeed");
    out.strideLength = r.number(path + ".strideLength");
    out.mouseSensitivity = r.number(path + ".mouseSensitivity");
    out.maxPitch = r.number(path + ".maxPitch");
    return r.ok();
}

void applyLook(Player& player, const MoveInput& in, const PlayerParams& params) {
    player.angle += in.mouseDx * params.mouseSensitivity;
    player.pitch = clampd(player.pitch - in.mouseDy * params.mouseSensitivity, -params.maxPitch, params.maxPitch);
}

std::optional<char> PlayerWalker::update(const Level& level, Player& player, double dt, const MoveInput& in,
                                         const PlayerParams& params) {
    const double turn = params.turnSpeed * dt;
    if (in.turnLeft) player.angle -= turn;
    if (in.turnRight) player.angle += turn;

    const double forward = (in.forward ? 1.0 : 0.0) - (in.back ? 1.0 : 0.0);
    const double side = (in.strafeRight ? 1.0 : 0.0) - (in.strafeLeft ? 1.0 : 0.0);
    if (forward == 0 && side == 0) return std::nullopt;

    const double speed = (in.run ? params.runSpeed : params.walkSpeed) * dt;
    const Vec2 step = walkStep(player.angle, forward, side, speed);
    const Vec2 before = player.pos;
    moveWithCollision(level, player.pos, step.x, step.y, params.radius);

    strideAccum_ += std::hypot(player.pos.x - before.x, player.pos.y - before.y);
    if (strideAccum_ < params.strideLength) return std::nullopt;
    strideAccum_ = 0;
    return level.at(static_cast<int>(std::floor(player.pos.x)), static_cast<int>(std::floor(player.pos.y)));
}

int findAimed(const Level& level, const Player& player, double eyeHeight, double reach,
              const std::vector<AimCandidate>& candidates) {
    const double cosPitch = std::cos(player.pitch);
    const Vec3 dir{std::cos(player.angle) * cosPitch, std::sin(player.pitch), std::sin(player.angle) * cosPitch};
    const double eyeX = player.pos.x, eyeZ = player.pos.y;
    double nearestHit = reach;
    int best = -1;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const AimCandidate& candidate = candidates[i];
        if (!candidate.enabled) continue;
        const double hit = rayHitsSphere({eyeX, eyeHeight, eyeZ}, dir, {candidate.at.x, candidate.height, candidate.at.y},
                                         candidate.radius, nearestHit);
        if (hit < 0) continue;
        // The wall check stops half a metre short of the prop, so an object standing against a wall stays visible.
        const double toX = candidate.at.x - eyeX, toZ = candidate.at.y - eyeZ;
        const double flatDistance = std::hypot(toX, toZ);
        const double backOff = flatDistance > 0.5 ? 0.5 / flatDistance : 1.0;
        if (!level.lineOfSight(eyeX, eyeZ, candidate.at.x - toX * backOff, candidate.at.y - toZ * backOff)) continue;
        nearestHit = hit;
        best = static_cast<int>(i);
    }
    return best;
}

}  // namespace mythos
