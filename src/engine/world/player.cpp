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

void applyLook(Player& p, const MoveInput& in, const PlayerParams& params) {
    p.angle += in.mouseDx * params.mouseSensitivity;
    p.pitch = clampd(p.pitch - in.mouseDy * params.mouseSensitivity, -params.maxPitch, params.maxPitch);
}

std::optional<char> PlayerWalker::update(const Level& level, Player& p, double dt, const MoveInput& in,
                                         const PlayerParams& params) {
    const double turn = params.turnSpeed * dt;
    if (in.turnLeft) p.angle -= turn;
    if (in.turnRight) p.angle += turn;

    const double fwd = (in.forward ? 1.0 : 0.0) - (in.back ? 1.0 : 0.0);
    const double side = (in.strafeRight ? 1.0 : 0.0) - (in.strafeLeft ? 1.0 : 0.0);
    if (fwd == 0 && side == 0) return std::nullopt;

    const double speed = (in.run ? params.runSpeed : params.walkSpeed) * dt;
    const Vec2 step = walkStep(p.angle, fwd, side, speed);
    const Vec2 before = p.pos;
    moveWithCollision(level, p.pos, step.x, step.y, params.radius);

    strideAccum_ += std::hypot(p.pos.x - before.x, p.pos.y - before.y);
    if (strideAccum_ < params.strideLength) return std::nullopt;
    strideAccum_ = 0;
    return level.at(static_cast<int>(std::floor(p.pos.x)), static_cast<int>(std::floor(p.pos.y)));
}

int findAimed(const Level& level, const Player& p, double eyeHeight, double reach, const std::vector<AimCandidate>& candidates) {
    const double cp = std::cos(p.pitch);
    const Vec3 dir{std::cos(p.angle) * cp, std::sin(p.pitch), std::sin(p.angle) * cp};
    const double ex = p.pos.x, ez = p.pos.y;
    double bestT = reach;
    int best = -1;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const AimCandidate& c = candidates[i];
        if (!c.enabled) continue;
        const double t = rayHitsSphere({ex, eyeHeight, ez}, dir, {c.at.x, c.height, c.at.y}, c.radius, bestT);
        if (t < 0) continue;
        const double cx = c.at.x - ex, cz = c.at.y - ez;
        const double flat = std::hypot(cx, cz);
        const double back = flat > 0.5 ? 0.5 / flat : 1.0;
        if (!level.lineOfSight(ex, ez, c.at.x - cx * back, c.at.y - cz * back)) continue;
        bestT = t;
        best = static_cast<int>(i);
    }
    return best;
}

}  // namespace mythos
