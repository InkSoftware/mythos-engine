#include "engine/world/body.h"

#include <algorithm>
#include <cmath>

namespace mythos {
namespace {

struct Vector {
    double x = 0, y = 0, z = 0;
};
Vector operator+(Vector a, Vector b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vector operator-(Vector a, Vector b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vector operator*(Vector a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(Vector a, Vector b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vector cross(Vector a, Vector b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double length(Vector a) { return std::sqrt(dot(a, a)); }
Vector unit(Vector a, Vector fallback = {0, 0, 1}) {
    const double len = length(a);
    return len > 1e-9 ? a * (1.0 / len) : fallback;
}

constexpr Vector kUp{0, 1, 0};

// A model's frame: +z along `fwd`, +y as upright as that allows, +x the model's left (cross(up, fwd)).
PartMatrix frame(Vector pos, Vector fwd, Vector up = kUp, double scale = 1.0) {
    fwd = unit(fwd);
    Vector x = cross(up, fwd);
    if (length(x) < 1e-6) x = cross({0, 0, 1}, fwd);  // looking straight along `up`
    x = unit(x);
    const Vector y = cross(fwd, x);
    PartMatrix p;
    const Vector cols[4] = {x * scale, y * scale, fwd * scale, pos};
    for (int col = 0; col < 4; ++col) {
        p.m[col * 4 + 0] = static_cast<float>(cols[col].x);
        p.m[col * 4 + 1] = static_cast<float>(cols[col].y);
        p.m[col * 4 + 2] = static_cast<float>(cols[col].z);
    }
    return p;
}

Vector dirOf(double yaw, double pitch) { return {std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)}; }

// HandSway: the shortest step it integrates over, and the gap (a pause or teleport) after which it restarts from the view.
constexpr double kMinStep = 1e-4;
constexpr double kResetGap = 0.25;

constexpr double kTwoPi = 6.283185307179586;

// Wraps an angle difference into [-pi, pi].
double wrapPi(double angle) { return std::remainder(angle, kTwoPi); }

// How fast the body yaw eases towards the view yaw (per second): quick while walking, lazy while standing.
constexpr double kTurnFollowWalking = 6.0;
constexpr double kTurnFollowStanding = 1.2;

// Pitch of the hand-held light is kept inside this so the beam never points straight up or down.
constexpr double kMaxHeldPitch = 1.5;

// The relaxed arm hangs this fraction of its full reach below the shoulder and a touch forward; swings half as far as the legs.
constexpr double kArmHangFraction = 0.93;
constexpr double kArmHangForward = 0.04;
constexpr double kArmSwingOfLeg = 0.5;
// The elbow bends down and this much outwards.
constexpr double kElbowOutward = 0.5;

}  // namespace

ArmIk solveArm(const double shoulder[3], const double target[3], double upper, double fore, const double bend[3]) {
    const Vector shoulderPos{shoulder[0], shoulder[1], shoulder[2]};
    const Vector targetPos{target[0], target[1], target[2]};
    const Vector bendDir{bend[0], bend[1], bend[2]};
    const Vector toTarget = targetPos - shoulderPos;
    double reach = length(toTarget);
    const Vector dir = unit(toTarget, {0, -1, 0});
    // Stay just inside the range the two bones can span (fully folded to fully stretched).
    reach = std::clamp(reach, std::abs(upper - fore) + 0.01, upper + fore - 0.001);
    // The elbow sits `along` the line and `off` it, in the plane the bend direction picks.
    const double along = (upper * upper - fore * fore + reach * reach) / (2.0 * reach);
    const double outFromLine = std::sqrt(std::max(0.0, upper * upper - along * along));
    Vector sideways = bendDir - dir * dot(bendDir, dir);
    sideways = unit(sideways, cross(dir, {1, 0, 0}));
    const Vector elbowPos = shoulderPos + dir * along + sideways * outFromLine;
    const Vector handPos = shoulderPos + dir * reach;
    return {{elbowPos.x, elbowPos.y, elbowPos.z}, {handPos.x, handPos.y, handPos.z}};
}

double turnBody(double bodyYaw, double viewYaw, double dt, bool walking, const BodyParams& params) {
    const double diff = wrapPi(viewYaw - bodyYaw);
    double yaw = bodyYaw;
    if (std::abs(diff) > params.turnLimit) yaw += diff - std::copysign(params.turnLimit, diff);  // dragged along
    const double follow = walking ? kTurnFollowWalking : kTurnFollowStanding;
    yaw += wrapPi(viewYaw - yaw) * std::min(1.0, dt * follow);
    return yaw;
}

namespace {

// The camera's axes, as the view shows them.
struct ViewAxes {
    Vector forward;  // where he looks
    Vector right;    // the camera's right: (-sin yaw, 0, cos yaw)
    Vector up;       // the view's up, which tips with the pitch
};

// The character's own axes on the floor.
struct BodyAxes {
    Vector feet;       // on the floor, a little behind the eye
    Vector chest;      // where the chest faces
    Vector charRight;  // his right hand (the model's +x is his left)
};

// Legs swing from the hip, opposite to each other, by `swing` radians at most.
void poseLegs(BodyPose& pose, const BodyAxes& body, double swing, const BodyParams& bt) {
    for (int side = 0; side < 2; ++side) {
        const double sign = side == 0 ? 1.0 : -1.0;  // +1: the model's left
        const double angle = sign * swing;
        const Vector hip = body.feet + kUp * bt.hipHeight + body.charRight * (-sign * bt.hipHalfSpacing);
        const Vector forward = body.chest * std::cos(angle) + kUp * std::sin(angle);
        const Vector up = kUp * std::cos(angle) - body.chest * std::sin(angle);
        (side == 0 ? pose.legL : pose.legR) = frame(hip, forward, up);
    }
}

// Where the light is held, in the view (it keeps its place on the screen whichever way he looks).
Vector heldGrip(const BodyInput& in, const Vector& eye, const ViewAxes& view, const HeldParams& ht) {
    const double right = (in.lighter ? ht.lighterRight : ht.right) + in.handSide;
    const double down = (in.lighter ? ht.lighterDown : ht.down) + in.handDown;
    const double forward = in.lighter ? ht.lighterForward : ht.forward;
    return eye + view.right * right + view.forward * forward - view.up * down;
}

// The right hand reaches for the grip (eased by `raise`); the left arm hangs and swings against the legs.
void poseArms(BodyPose& pose, const BodyAxes& body, const Vector& grip, double swing, double raise, const BodyParams& bt) {
    const double reach = bt.upperArm + bt.foreArm;
    for (int side = 0; side < 2; ++side) {
        const bool rightArm = side == 1;
        const double sideSign = rightArm ? 1.0 : -1.0;
        const Vector shoulder = body.feet + kUp * bt.shoulderHeight + body.charRight * (sideSign * bt.shoulderHalfSpacing);
        const double armSwing = (rightArm ? -1.0 : 1.0) * kArmSwingOfLeg * swing;  // opposite to the leg on its side
        const Vector hang = shoulder - kUp * (reach * kArmHangFraction) + body.chest * (kArmHangForward + reach * armSwing);
        Vector target = hang;
        if (rightArm) {
            const double t = std::clamp(raise, 0.0, 1.0);
            const double eased = t * t * (3.0 - 2.0 * t);  // smoothstep
            target = hang * (1.0 - eased) + grip * eased;
        }
        const Vector bend = (kUp * -1.0) + body.charRight * (sideSign * kElbowOutward);
        const double shoulderArr[3] = {shoulder.x, shoulder.y, shoulder.z};
        const double targetArr[3] = {target.x, target.y, target.z};
        const double bendArr[3] = {bend.x, bend.y, bend.z};
        const ArmIk ik = solveArm(shoulderArr, targetArr, bt.upperArm, bt.foreArm, bendArr);
        const Vector elbow{ik.elbow[0], ik.elbow[1], ik.elbow[2]}, hand{ik.hand[0], ik.hand[1], ik.hand[2]};
        (rightArm ? pose.upperArmR : pose.upperArmL) = frame(shoulder, elbow - shoulder);
        (rightArm ? pose.foreArmR : pose.foreArmL) = frame(elbow, hand - elbow);
    }
}

// A lighter stands upright, leaning back a little, with the flame on top. Held in the view, not in the world.
// The model stands upright with its wide face towards +x and its lid hinged to one side (see the lighter model): that
// face is turned towards him, a little towards the middle of the screen (lighterTurn), and leaned back (lighterTilt).
void poseLighter(BodyPose& pose, const Vector& grip, const ViewAxes& view, const HeldParams& ht, double time) {
    const Vector toLeft = view.right * -1.0;
    const Vector modelZ = toLeft * std::cos(ht.lighterTurn) + view.forward * std::sin(ht.lighterTurn);
    const double cosTilt = std::cos(ht.lighterTilt), sinTilt = std::sin(ht.lighterTilt);
    const auto lean = [&](Vector v) {  // about the view's right axis: the top away from him
        const double alongUp = dot(v, view.up), alongForward = dot(v, view.forward);
        return v - view.up * alongUp - view.forward * alongForward +
               (view.up * cosTilt + view.forward * sinTilt) * alongUp +
               (view.forward * cosTilt - view.up * sinTilt) * alongForward;
    };
    const Vector lighterForward = unit(lean(modelZ)), lighterUp = unit(lean(view.up));
    const Vector lighterX = unit(cross(lighterUp, lighterForward));
    pose.held = frame(grip, lighterForward, lighterUp * std::cos(ht.lighterRoll) + lighterX * std::sin(ht.lighterRoll));
    const Vector up = {pose.held.m[4], pose.held.m[5], pose.held.m[6]};
    const Vector zAxis = {pose.held.m[8], pose.held.m[9], pose.held.m[10]};
    // The flame's size wobbles with a few unrelated sine waves, so it never repeats visibly.
    const double flicker = 0.85 + 0.15 * std::sin(time * 23.0) * std::sin(time * 7.1) + 0.1 * std::sin(time * 31.0);
    pose.flame = frame(grip + up * ht.flameHeight + zAxis * ht.flameShift, view.forward, view.up, flicker);
}

}  // namespace

BodyPose poseBody(const BodyInput& in, const BodyParams& bt, const HeldParams& ht) {
    BodyPose pose;
    const Vector viewFwd = dirOf(in.yaw, in.pitch);
    const Vector viewFlat = unit({std::cos(in.yaw), 0, std::sin(in.yaw)});
    const Vector eye{in.eyeX, in.eyeHeight, in.eyeZ};

    BodyAxes body;
    body.feet = Vector{in.eyeX, 0, in.eyeZ} - viewFlat * bt.eyeAhead;
    body.chest = {std::cos(in.bodyYaw), 0, std::sin(in.bodyYaw)};
    body.charRight = cross(body.chest, kUp);
    pose.torso = frame(body.feet, body.chest);
    pose.head = frame(body.feet + kUp * bt.neckHeight, viewFwd);

    // Legs (and the arms against them) swing in proportion to how much he walks.
    const double swing = bt.legSwing * in.walk * std::sin(in.stride);
    poseLegs(pose, body, swing, bt);

    // The right hand brings the light up to where it is held in view (the same place the renderer's view shows it).
    ViewAxes view;
    view.forward = viewFwd;
    view.right = cross(viewFlat, kUp);
    view.up = unit(cross(view.right, viewFwd), kUp);
    const Vector grip = heldGrip(in, eye, view, ht);
    pose.handX = grip.x;
    pose.handY = grip.y;
    pose.handZ = grip.z;
    poseArms(pose, body, grip, swing, in.raise, bt);

    // A torch points where the beam goes; a lighter stands upright.
    if (in.lighter) {
        poseLighter(pose, grip, view, ht, in.time);
    } else {
        const Vector beamDir = dirOf(in.yaw + in.handYaw, std::clamp(in.pitch + in.handPitch, -kMaxHeldPitch, kMaxHeldPitch));
        pose.held = frame(grip, beamDir);
    }
    return pose;
}

bool loadBodyParams(json::Reader& r, const std::string& path, BodyParams& out) {
    out.neckHeight = r.number(path + ".neckHeight");
    out.eyeAhead = r.number(path + ".eyeAhead");
    out.hipHeight = r.number(path + ".hipHeight");
    out.hipHalfSpacing = r.number(path + ".hipHalfSpacing");
    out.shoulderHeight = r.number(path + ".shoulderHeight");
    out.shoulderHalfSpacing = r.number(path + ".shoulderHalfSpacing");
    out.upperArm = r.number(path + ".upperArm");
    out.foreArm = r.number(path + ".foreArm");
    out.legSwing = r.number(path + ".legSwing");
    out.turnLimit = r.number(path + ".turnLimit");
    out.raiseSeconds = r.number(path + ".raiseSeconds");
    if (r.ok() && (out.upperArm <= 0 || out.foreArm <= 0 || out.raiseSeconds <= 0 || out.turnLimit <= 0))
        r.fail(path + ".upperArm, foreArm, raiseSeconds and turnLimit must be positive");
    return r.ok();
}

bool loadHandSwayParams(json::Reader& r, const std::string& path, HandSwayParams& out) {
    const struct {
        const char* key;
        double* member;  // the field of `out` that this key fills
    } fields[] = {{"springK", &out.springK},     {"springDamp", &out.springDamp}, {"turnLag", &out.turnLag},
                  {"yawLimit", &out.yawLimit},   {"pitchLimit", &out.pitchLimit}, {"walkSpeed", &out.walkSpeed},
                  {"walkEase", &out.walkEase},   {"strideRate", &out.strideRate}, {"walkYaw", &out.walkYaw},
                  {"walkPitch", &out.walkPitch}, {"walkSide", &out.walkSide},     {"walkDown", &out.walkDown},
                  {"idleYaw", &out.idleYaw},     {"idleBreath", &out.idleBreath}, {"idleDrift", &out.idleDrift},
                  {"idleSide", &out.idleSide},   {"idleDown", &out.idleDown},     {"tremorStill", &out.tremorStill},
                  {"tremorWalk", &out.tremorWalk}, {"bob", &out.bob}};
    for (const auto& field : fields) *field.member = r.number(path + "." + field.key);
    if (r.ok() && (out.springK <= 0 || out.springDamp < 0 || out.walkSpeed <= 0 || out.yawLimit < 0 || out.pitchLimit < 0))
        r.fail(path + ".springK and walkSpeed must be positive; springDamp and the limits not negative");
    return r.ok();
}

HandPose HandSway::update(double dt, double time, double x, double z, double viewYaw, double viewPitch,
                          const HandSwayParams& params) {
    const double step = std::max(dt, kMinStep);
    if (!primed_ || dt > kResetGap) {
        lastYaw_ = viewYaw;
        lastPitch_ = viewPitch;
        lastX_ = x;
        lastZ_ = z;
        primed_ = true;
    }
    const double turnYaw = wrapPi(viewYaw - lastYaw_), turnPitch = viewPitch - lastPitch_;
    const double speed = std::hypot(x - lastX_, z - lastZ_) / step;
    lastYaw_ = viewYaw;
    lastPitch_ = viewPitch;
    lastX_ = x;
    lastZ_ = z;
    // Spring-damper: turning the view pulls the beam the other way, then it catches up.
    yawV_ += (-params.springK * yaw_ - params.springDamp * yawV_) * step;
    yaw_ -= turnYaw * params.turnLag;
    pitchV_ += (-params.springK * pitch_ - params.springDamp * pitchV_) * step;
    pitch_ -= turnPitch * params.turnLag;
    yaw_ = std::clamp(yaw_ + yawV_ * step, -params.yawLimit, params.yawLimit);
    pitch_ = std::clamp(pitch_ + pitchV_ * step, -params.pitchLimit, params.pitchLimit);
    const double moving = std::min(1.0, speed / params.walkSpeed);
    walk_ += (moving - walk_) * std::min(1.0, step * params.walkEase);
    stride_ += speed * step * params.strideRate;

    const double still = 1.0 - walk_;  // idle: breathing and a hand that drifts and re-settles
    const double tremor = params.tremorStill + params.tremorWalk * walk_;
    const double breath = std::sin(time * 1.35);  // about one breath every 4.6 s
    const double drift = std::sin(time * 0.47) + 0.7 * std::sin(time * 0.83 + 1.7) + 0.4 * std::sin(time * 1.9 + 0.4);
    HandPose pose;
    pose.stride = stride_;
    pose.walk = walk_;
    pose.yaw = yaw_ + walk_ * params.walkYaw * std::sin(stride_ * 0.5) + still * params.idleYaw * drift +
            tremor * (std::sin(time * 1.7) + 0.6 * std::sin(time * 4.1 + 1.0));
    pose.pitch = pitch_ + walk_ * params.walkPitch * std::sin(stride_ + 0.6) +
              still * (params.idleBreath * breath + params.idleDrift * (std::sin(time * 0.61 + 0.9) + 0.6 * std::sin(time * 1.43))) +
              tremor * (std::sin(time * 1.3 + 2.0) + 0.6 * std::sin(time * 3.7));
    pose.side = walk_ * params.walkSide * std::sin(stride_ * 0.5) + still * params.idleSide * drift;
    pose.down = walk_ * params.walkDown * (0.5 - 0.5 * std::cos(stride_ * 2.0)) * 0.5 + params.bob * std::sin(time * 1.1) +
             still * params.idleDown * breath;
    return pose;
}

void BodyMotion::update(double dt, double viewYaw, bool walking, bool lightUp, const BodyParams& params) {
    if (!primed_) {
        yaw_ = viewYaw;
        primed_ = true;
    }
    yaw_ = turnBody(yaw_, viewYaw, dt, walking, params);
    raise_ += ((lightUp ? 1.0 : 0.0) - raise_) * std::min(1.0, dt / params.raiseSeconds);
}

bool loadHeldParams(json::Reader& r, const std::string& path, HeldParams& out) {
    out.right = r.number(path + ".right");
    out.down = r.number(path + ".down");
    out.forward = r.number(path + ".forward");
    out.lighterRight = r.number(path + ".lighterRight");
    out.lighterDown = r.number(path + ".lighterDown");
    out.lighterForward = r.number(path + ".lighterForward");
    out.lighterTilt = r.number(path + ".lighterTilt");
    out.lighterRoll = r.number(path + ".lighterRoll");
    out.lighterTurn = r.number(path + ".lighterTurn");
    out.flameHeight = r.number(path + ".flameHeight");
    out.flameShift = r.number(path + ".flameShift");
    return r.ok();
}

}  // namespace mythos
