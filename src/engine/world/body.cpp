#include "engine/world/body.h"

#include <algorithm>
#include <cmath>

namespace mythos {
namespace {

struct V {
    double x = 0, y = 0, z = 0;
};
V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V operator*(V a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double length(V a) { return std::sqrt(dot(a, a)); }
V unit(V a, V fallback = {0, 0, 1}) {
    const double l = length(a);
    return l > 1e-9 ? a * (1.0 / l) : fallback;
}

constexpr V kUp{0, 1, 0};

// A model's frame: +z along `fwd`, +y as upright as that allows, +x the model's left (cross(up, fwd)).
PartMatrix frame(V pos, V fwd, V up = kUp, double scale = 1.0) {
    fwd = unit(fwd);
    V x = cross(up, fwd);
    if (length(x) < 1e-6) x = cross({0, 0, 1}, fwd);  // looking straight along `up`
    x = unit(x);
    const V y = cross(fwd, x);
    PartMatrix p;
    const V cols[4] = {x * scale, y * scale, fwd * scale, pos};
    for (int c = 0; c < 4; ++c) {
        p.m[c * 4 + 0] = static_cast<float>(cols[c].x);
        p.m[c * 4 + 1] = static_cast<float>(cols[c].y);
        p.m[c * 4 + 2] = static_cast<float>(cols[c].z);
    }
    return p;
}

V dirOf(double yaw, double pitch) { return {std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)}; }

double wrapPi(double a) { return std::remainder(a, 6.283185307179586); }

}  // namespace

ArmIk solveArm(const double shoulder[3], const double target[3], double upper, double fore, const double bend[3]) {
    const V s{shoulder[0], shoulder[1], shoulder[2]}, t{target[0], target[1], target[2]}, b{bend[0], bend[1], bend[2]};
    V to = t - s;
    double d = length(to);
    const V dir = unit(to, {0, -1, 0});
    d = std::clamp(d, std::abs(upper - fore) + 0.01, upper + fore - 0.001);
    // The elbow sits `a` along the line and `h` off it, in the plane the bend direction picks.
    const double a = (upper * upper - fore * fore + d * d) / (2.0 * d);
    const double h = std::sqrt(std::max(0.0, upper * upper - a * a));
    V off = b - dir * dot(b, dir);
    off = unit(off, cross(dir, {1, 0, 0}));
    const V e = s + dir * a + off * h;
    const V hand = s + dir * d;
    return {{e.x, e.y, e.z}, {hand.x, hand.y, hand.z}};
}

double turnBody(double bodyYaw, double viewYaw, double dt, bool walking, const BodyParams& t) {
    const double diff = wrapPi(viewYaw - bodyYaw);
    double yaw = bodyYaw;
    if (std::abs(diff) > t.turnLimit) yaw += diff - std::copysign(t.turnLimit, diff);  // dragged along
    const double follow = walking ? 6.0 : 1.2;
    yaw += wrapPi(viewYaw - yaw) * std::min(1.0, dt * follow);
    return yaw;
}

BodyPose poseBody(const BodyInput& in, const BodyParams& bt, const HeldParams& ht) {
    BodyPose pose;
    const V viewFwd = dirOf(in.yaw, in.pitch);
    const V viewFlat = unit({std::cos(in.yaw), 0, std::sin(in.yaw)});
    const V eye{in.eyeX, in.eyeHeight, in.eyeZ};
    const V feet = V{in.eyeX, 0, in.eyeZ} - viewFlat * bt.eyeAhead;

    const V chest{std::cos(in.bodyYaw), 0, std::sin(in.bodyYaw)};
    const V charRight = cross(chest, kUp);  // his right hand (the model's +x is his left)
    pose.torso = frame(feet, chest);
    pose.head = frame(feet + kUp * bt.neckHeight, viewFwd);

    // Legs swing from the hip, opposite to each other, in proportion to how much he walks.
    const double swing = bt.legSwing * in.walk * std::sin(in.stride);
    for (int side = 0; side < 2; ++side) {
        const double sign = side == 0 ? 1.0 : -1.0;  // +1: the model's left
        const double th = sign * swing;
        const V hip = feet + kUp * bt.hipHeight + charRight * (-sign * bt.hipHalfSpacing);
        const V f = chest * std::cos(th) + kUp * std::sin(th);
        const V u = kUp * std::cos(th) - chest * std::sin(th);
        PartMatrix m = frame(hip, f, u);
        (side == 0 ? pose.legL : pose.legR) = m;
    }

    // The right hand brings the light up to where it is held in view (the same place the renderer's view shows it);
    // the left arm hangs and swings against the legs.
    const V flashDir = dirOf(in.yaw + in.handYaw, std::clamp(in.pitch + in.handPitch, -1.5, 1.5));
    const V eyeRight = cross(viewFlat, kUp);  // = the camera's right: (-sin yaw, 0, cos yaw)
    const double r = (in.lighter ? ht.lighterRight : ht.right) + in.handSide;
    const double dn = (in.lighter ? ht.lighterDown : ht.down) + in.handDown;
    const double fw = in.lighter ? ht.lighterForward : ht.forward;
    const V camUp = unit(cross(eyeRight, viewFwd), kUp);  // the view's up, which tips with the pitch
    const V grip = eye + eyeRight * r + viewFwd * fw - camUp * dn;  // held in the view: it keeps its place on the screen
    pose.handX = grip.x;
    pose.handY = grip.y;
    pose.handZ = grip.z;

    const double reach = bt.upperArm + bt.foreArm;
    for (int side = 0; side < 2; ++side) {
        const bool rightArm = side == 1;
        const double sgn = rightArm ? 1.0 : -1.0;
        const V shoulder = feet + kUp * bt.shoulderHeight + charRight * (sgn * bt.shoulderHalfSpacing);
        const double armSwing = (rightArm ? -1.0 : 1.0) * 0.5 * swing;  // opposite to the leg on its side
        V hang = shoulder - kUp * (reach * 0.93) + chest * (0.04 + reach * armSwing);
        V target = hang;
        if (rightArm) {
            const double k = std::clamp(in.raise, 0.0, 1.0);
            const double e = k * k * (3.0 - 2.0 * k);
            target = hang * (1.0 - e) + grip * e;
        }
        const V bend = (kUp * -1.0) + charRight * (sgn * 0.5);
        const double s3[3] = {shoulder.x, shoulder.y, shoulder.z}, t3[3] = {target.x, target.y, target.z}, b3[3] = {bend.x, bend.y, bend.z};
        const ArmIk ik = solveArm(s3, t3, bt.upperArm, bt.foreArm, b3);
        const V elbow{ik.elbow[0], ik.elbow[1], ik.elbow[2]}, hand{ik.hand[0], ik.hand[1], ik.hand[2]};
        (rightArm ? pose.upperArmR : pose.upperArmL) = frame(shoulder, elbow - shoulder);
        (rightArm ? pose.foreArmR : pose.foreArmL) = frame(elbow, hand - elbow);
    }

    // The light: a torch points where the beam goes; a lighter stands upright, leaning back a little, with the flame on top.
    if (in.lighter) {
        // Held in the view, not in the world: it keeps its place on the screen whichever way he looks. The model stands
        // upright with its wide face towards +x and its lid hinged to one side (see the lighter model): that face is
        // turned towards him, a little towards the middle of the screen (lighterTurn), and leaned back (lighterTilt).
        const V toLeft = eyeRight * -1.0;
        const V zAxis0 = toLeft * std::cos(ht.lighterTurn) + viewFwd * std::sin(ht.lighterTurn);  // the model's z
        const double c = std::cos(ht.lighterTilt), sn = std::sin(ht.lighterTilt);
        const auto lean = [&](V v) {  // about the view's right axis: the top away from him
            const double u = dot(v, camUp), f = dot(v, viewFwd);
            return v - camUp * u - viewFwd * f + (camUp * c + viewFwd * sn) * u + (viewFwd * c - camUp * sn) * f;
        };
        const V lf = unit(lean(zAxis0)), lu = unit(lean(camUp));
        const V lx = unit(cross(lu, lf));
        pose.held = frame(grip, lf, lu * std::cos(ht.lighterRoll) + lx * std::sin(ht.lighterRoll));
        const V up = {pose.held.m[4], pose.held.m[5], pose.held.m[6]};
        const double flick = 0.85 + 0.15 * std::sin(in.time * 23.0) * std::sin(in.time * 7.1) + 0.1 * std::sin(in.time * 31.0);
        const V zAxis = {pose.held.m[8], pose.held.m[9], pose.held.m[10]};
        pose.flame = frame(grip + up * ht.flameHeight + zAxis * ht.flameShift, viewFwd, camUp, flick);
    } else {
        pose.held = frame(grip, flashDir);
    }
    return pose;
}

bool loadBodyParams(json::Reader& r, const std::string& path, BodyParams& t) {
    t.neckHeight = r.number(path + ".neckHeight");
    t.eyeAhead = r.number(path + ".eyeAhead");
    t.hipHeight = r.number(path + ".hipHeight");
    t.hipHalfSpacing = r.number(path + ".hipHalfSpacing");
    t.shoulderHeight = r.number(path + ".shoulderHeight");
    t.shoulderHalfSpacing = r.number(path + ".shoulderHalfSpacing");
    t.upperArm = r.number(path + ".upperArm");
    t.foreArm = r.number(path + ".foreArm");
    t.legSwing = r.number(path + ".legSwing");
    t.turnLimit = r.number(path + ".turnLimit");
    t.raiseSeconds = r.number(path + ".raiseSeconds");
    if (r.ok() && (t.upperArm <= 0 || t.foreArm <= 0 || t.raiseSeconds <= 0 || t.turnLimit <= 0))
        r.fail(path + ".upperArm, foreArm, raiseSeconds and turnLimit must be positive");
    return r.ok();
}

bool loadHandSwayParams(json::Reader& r, const std::string& path, HandSwayParams& t) {
    const struct {
        const char* key;
        double* out;
    } fields[] = {{"springK", &t.springK},     {"springDamp", &t.springDamp}, {"turnLag", &t.turnLag},
                  {"yawLimit", &t.yawLimit},   {"pitchLimit", &t.pitchLimit}, {"walkSpeed", &t.walkSpeed},
                  {"walkEase", &t.walkEase},   {"strideRate", &t.strideRate}, {"walkYaw", &t.walkYaw},
                  {"walkPitch", &t.walkPitch}, {"walkSide", &t.walkSide},     {"walkDown", &t.walkDown},
                  {"idleYaw", &t.idleYaw},     {"idleBreath", &t.idleBreath}, {"idleDrift", &t.idleDrift},
                  {"idleSide", &t.idleSide},   {"idleDown", &t.idleDown},     {"tremorStill", &t.tremorStill},
                  {"tremorWalk", &t.tremorWalk}, {"bob", &t.bob}};
    for (const auto& f : fields) *f.out = r.number(path + "." + f.key);
    if (r.ok() && (t.springK <= 0 || t.springDamp < 0 || t.walkSpeed <= 0 || t.yawLimit < 0 || t.pitchLimit < 0))
        r.fail(path + ".springK and walkSpeed must be positive; springDamp and the limits not negative");
    return r.ok();
}

HandPose HandSway::update(double dt, double time, double x, double z, double viewYaw, double viewPitch,
                          const HandSwayParams& p) {
    const double h = std::max(dt, 1e-4);
    if (!primed_ || dt > 0.25) {
        lastYaw_ = viewYaw;
        lastPitch_ = viewPitch;
        lastX_ = x;
        lastZ_ = z;
        primed_ = true;
    }
    const double turnYaw = wrapPi(viewYaw - lastYaw_), turnPitch = viewPitch - lastPitch_;
    const double speed = std::hypot(x - lastX_, z - lastZ_) / h;
    lastYaw_ = viewYaw;
    lastPitch_ = viewPitch;
    lastX_ = x;
    lastZ_ = z;
    // Spring-damper: turning the view pulls the beam the other way, then it catches up.
    yawV_ += (-p.springK * yaw_ - p.springDamp * yawV_) * h;
    yaw_ -= turnYaw * p.turnLag;
    pitchV_ += (-p.springK * pitch_ - p.springDamp * pitchV_) * h;
    pitch_ -= turnPitch * p.turnLag;
    yaw_ = std::clamp(yaw_ + yawV_ * h, -p.yawLimit, p.yawLimit);
    pitch_ = std::clamp(pitch_ + pitchV_ * h, -p.pitchLimit, p.pitchLimit);
    const double moving = std::min(1.0, speed / p.walkSpeed);
    walk_ += (moving - walk_) * std::min(1.0, h * p.walkEase);
    stride_ += speed * h * p.strideRate;

    const double t = time;
    const double still = 1.0 - walk_;  // idle: breathing and a hand that drifts and re-settles
    const double tremor = p.tremorStill + p.tremorWalk * walk_;
    const double breath = std::sin(t * 1.35);  // about one breath every 4.6 s
    const double drift = std::sin(t * 0.47) + 0.7 * std::sin(t * 0.83 + 1.7) + 0.4 * std::sin(t * 1.9 + 0.4);
    HandPose o;
    o.stride = stride_;
    o.walk = walk_;
    o.yaw = yaw_ + walk_ * p.walkYaw * std::sin(stride_ * 0.5) + still * p.idleYaw * drift +
            tremor * (std::sin(t * 1.7) + 0.6 * std::sin(t * 4.1 + 1.0));
    o.pitch = pitch_ + walk_ * p.walkPitch * std::sin(stride_ + 0.6) +
              still * (p.idleBreath * breath + p.idleDrift * (std::sin(t * 0.61 + 0.9) + 0.6 * std::sin(t * 1.43))) +
              tremor * (std::sin(t * 1.3 + 2.0) + 0.6 * std::sin(t * 3.7));
    o.side = walk_ * p.walkSide * std::sin(stride_ * 0.5) + still * p.idleSide * drift;
    o.down = walk_ * p.walkDown * (0.5 - 0.5 * std::cos(stride_ * 2.0)) * 0.5 + p.bob * std::sin(t * 1.1) +
             still * p.idleDown * breath;
    return o;
}

void BodyMotion::update(double dt, double viewYaw, bool walking, bool lightUp, const BodyParams& t) {
    if (!primed_) {
        yaw_ = viewYaw;
        primed_ = true;
    }
    yaw_ = turnBody(yaw_, viewYaw, dt, walking, t);
    raise_ += ((lightUp ? 1.0 : 0.0) - raise_) * std::min(1.0, dt / t.raiseSeconds);
}

bool loadHeldParams(json::Reader& r, const std::string& path, HeldParams& t) {
    t.right = r.number(path + ".right");
    t.down = r.number(path + ".down");
    t.forward = r.number(path + ".forward");
    t.lighterRight = r.number(path + ".lighterRight");
    t.lighterDown = r.number(path + ".lighterDown");
    t.lighterForward = r.number(path + ".lighterForward");
    t.lighterTilt = r.number(path + ".lighterTilt");
    t.lighterRoll = r.number(path + ".lighterRoll");
    t.lighterTurn = r.number(path + ".lighterTurn");
    t.flameHeight = r.number(path + ".flameHeight");
    t.flameShift = r.number(path + ".flameShift");
    return r.ok();
}

}  // namespace mythos
