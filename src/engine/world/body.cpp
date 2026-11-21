#include "engine/world/body.h"

#include <algorithm>
#include <cmath>

namespace mythos {
namespace {
// Small local vector type; kept private so the engine has no second public
// vector.
struct Vector {
  double x = 0, y = 0, z = 0;
};

Vector operator+(const Vector a, const Vector b) {
  return {.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z};
}

Vector operator-(const Vector a, const Vector b) {
  return {.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
}

Vector operator*(const Vector a, const double s) {
  return {.x = a.x * s, .y = a.y * s, .z = a.z * s};
}

double Dot(const Vector a, const Vector b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector Cross(const Vector a, const Vector b) {
  return {.x = a.y * b.z - a.z * b.y,
          .y = a.z * b.x - a.x * b.z,
          .z = a.x * b.y - a.y * b.x};
}

double Length(const Vector a) { return std::sqrt(Dot(a, a)); }

Vector Unit(const Vector a, const Vector fallback = {.x = 0, .y = 0, .z = 1}) {
  const double len = Length(a);
  return len > 1e-9 ? a * (1.0 / len) : fallback;
}

constexpr Vector kUp{.x = 0, .y = 1, .z = 0};

// A model's frame: +z along fwd, +y as upright as that allows, +x the model's
// left (cross(up, fwd)).
PartMatrix Frame(const Vector pos, Vector fwd, const Vector up = kUp,
                 const double scale = 1.0) {
  fwd = Unit(fwd);
  Vector x = Cross(up, fwd);
  // Looking straight along up: pick another reference axis.
  if (Length(x) < 1e-6) {
    x = Cross({.x = 0, .y = 0, .z = 1}, fwd);
  }
  x = Unit(x);
  const Vector y = Cross(fwd, x);
  PartMatrix p;
  const Vector cols[4] = {x * scale, y * scale, fwd * scale, pos};
  for (int col = 0; col < 4; ++col) {
    p.m[col * 4 + 0] = static_cast<float>(cols[col].x);
    p.m[col * 4 + 1] = static_cast<float>(cols[col].y);
    p.m[col * 4 + 2] = static_cast<float>(cols[col].z);
  }
  return p;
}

// Unit direction for a yaw and pitch: yaw is (cos, sin) on the x, z plane.
Vector DirOf(const double yaw, const double pitch) {
  return {.x = std::cos(yaw) * std::cos(pitch),
          .y = std::sin(pitch),
          .z = std::sin(yaw) * std::cos(pitch)};
}

// HandSway: the shortest step it integrates over, and the gap (a pause or
// teleport) after which it restarts from the view.
constexpr double kMinStep = 1e-4;
constexpr double kResetGap = 0.25;

constexpr double kTwoPi = 6.283185307179586;

// Wraps an angle difference into [-pi, pi].
double WrapPi(const double angle) { return std::remainder(angle, kTwoPi); }

// How fast the body yaw eases toward the view yaw (per second): quick while
// walking, lazy while standing.
constexpr double kTurnFollowWalking = 6.0;
constexpr double kTurnFollowStanding = 1.2;

// The hand-held light's pitch is kept inside this so the beam never points
// straight up or down.
constexpr double kMaxHeldPitch = 1.5;

// The relaxed arm hangs this fraction of its full reach below the shoulder and
// a touch forward, and swings half as far as the legs.
constexpr double kArmHangFraction = 0.93;
constexpr double kArmHangForward = 0.04;
constexpr double kArmSwingOfLeg = 0.5;

// The elbow bends down and this much outwards.
constexpr double kElbowOutward = 0.5;
}

ArmIk SolveArm(const double shoulder[3], const double target[3],
               const double upper, const double fore, const double bend[3]) {
  const Vector shoulder_pos{
      .x = shoulder[0], .y = shoulder[1], .z = shoulder[2]};
  const Vector target_pos{.x = target[0], .y = target[1], .z = target[2]};
  const Vector bend_dir{.x = bend[0], .y = bend[1], .z = bend[2]};
  const Vector to_target = target_pos - shoulder_pos;
  double reach = Length(to_target);
  const Vector dir = Unit(to_target, {.x = 0, .y = -1, .z = 0});

  // Stay just inside the range the two bones can span (fully folded to fully
  // stretched).
  reach =
      std::clamp(reach, std::abs(upper - fore) + 0.01, upper + fore - 0.001);

  // Law of cosines: the elbow sits "along" the shoulder-target line and
  // "out_from_line" off it, in the plane the bend direction picks.
  const double along =
      (upper * upper - fore * fore + reach * reach) / (2.0 * reach);
  const double out_from_line =
      std::sqrt(std::max(0.0, upper * upper - along * along));
  Vector sideways = bend_dir - dir * Dot(bend_dir, dir);
  sideways = Unit(sideways, Cross(dir, {.x = 1, .y = 0, .z = 0}));
  const Vector elbow_pos =
      shoulder_pos + dir * along + sideways * out_from_line;
  const Vector hand_pos = shoulder_pos + dir * reach;
  return {.elbow = {elbow_pos.x, elbow_pos.y, elbow_pos.z},
          .hand = {hand_pos.x, hand_pos.y, hand_pos.z}};
}

double TurnBody(const double body_yaw, const double view_yaw, const double dt,
                const bool walking, const BodyParams& params) {
  const double diff = WrapPi(view_yaw - body_yaw);
  double yaw = body_yaw;
  if (std::abs(diff) > params.turn_limit) {
    yaw += diff - std::copysign(params.turn_limit, diff);
  }
  const double follow = walking ? kTurnFollowWalking : kTurnFollowStanding;
  yaw += WrapPi(view_yaw - yaw) * std::min(1.0, dt * follow);
  return yaw;
}

namespace {
// The camera's axes, as the view shows them.
struct ViewAxes {
  Vector forward;
  Vector right;
  Vector up;
};

// The character's own axes on the floor; char_right is its right hand (the
// model's +x is its left).
struct BodyAxes {
  Vector feet;
  Vector chest;
  Vector char_right;
};

// Legs swing from the hip, opposite to each other, by up to swing radians.
void PoseLegs(BodyPose& pose, const BodyAxes& body, const double swing,
              const BodyParams& bt) {
  for (int side = 0; side < 2; ++side) {
    const double sign = side == 0 ? 1.0 : -1.0;
    const double angle = sign * swing;
    const Vector hip = body.feet + kUp * bt.hip_height +
                       body.char_right * (-sign * bt.hip_half_spacing);
    const Vector forward = body.chest * std::cos(angle) + kUp * std::sin(angle);
    const Vector up = kUp * std::cos(angle) - body.chest * std::sin(angle);
    (side == 0 ? pose.leg_l : pose.leg_r) = Frame(hip, forward, up);
  }
}

// Where the light is held, in the view (it keeps its place on the screen
// whichever way the player looks).
Vector HeldGrip(const BodyInput& in, const Vector& eye, const ViewAxes& view,
                const HeldParams& ht) {
  const double right =
      (in.lighter ? ht.lighter_right : ht.right) + in.hand_side;
  const double down = (in.lighter ? ht.lighter_down : ht.down) + in.hand_down;
  const double forward = in.lighter ? ht.lighter_forward : ht.forward;
  return eye + view.right * right + view.forward * forward - view.up * down;
}

// The right hand reaches for the grip, eased by raise; the left arm hangs and
// swings against the legs.
void PoseArms(BodyPose& pose, const BodyAxes& body, const Vector& grip,
              const double swing, const double raise, const BodyParams& bt) {
  const double reach = bt.upper_arm + bt.fore_arm;
  for (int side = 0; side < 2; ++side) {
    const bool right_arm = side == 1;
    const double side_sign = right_arm ? 1.0 : -1.0;
    const Vector shoulder =
        body.feet + kUp * bt.shoulder_height +
        body.char_right * (side_sign * bt.shoulder_half_spacing);
    const double arm_swing = (right_arm ? -1.0 : 1.0) * kArmSwingOfLeg * swing;
    const Vector hang = shoulder - kUp * (reach * kArmHangFraction) +
                        body.chest * (kArmHangForward + reach * arm_swing);
    Vector target = hang;
    if (right_arm) {
      const double t = std::clamp(raise, 0.0, 1.0);
      // Smoothstep.
      const double eased = t * t * (3.0 - 2.0 * t);
      target = hang * (1.0 - eased) + grip * eased;
    }
    const Vector bend =
        (kUp * -1.0) + body.char_right * (side_sign * kElbowOutward);
    const double shoulder_arr[3] = {shoulder.x, shoulder.y, shoulder.z};
    const double target_arr[3] = {target.x, target.y, target.z};
    const double bend_arr[3] = {bend.x, bend.y, bend.z};
    const ArmIk ik =
        SolveArm(shoulder_arr, target_arr, bt.upper_arm, bt.fore_arm, bend_arr);
    const Vector elbow{.x = ik.elbow[0], .y = ik.elbow[1], .z = ik.elbow[2]},
        hand{.x = ik.hand[0], .y = ik.hand[1], .z = ik.hand[2]};
    (right_arm ? pose.upper_arm_r : pose.upper_arm_l) =
        Frame(shoulder, elbow - shoulder);
    (right_arm ? pose.fore_arm_r : pose.fore_arm_l) =
        Frame(elbow, hand - elbow);
  }
}

// A lighter stands upright, leaning back a little, with the flame on top, held
// in the view rather than the world. Its model has the wide face toward +x;
// that face is turned toward the player, a little toward the middle of the
// screen (lighter_turn) and leaned back (lighter_tilt).
void PoseLighter(BodyPose& pose, const Vector& grip, const ViewAxes& view,
                 const HeldParams& ht, const double time) {
  const Vector to_left = view.right * -1.0;
  const Vector model_z = to_left * std::cos(ht.lighter_turn) +
                         view.forward * std::sin(ht.lighter_turn);
  const double cos_tilt = std::cos(ht.lighter_tilt),
               sin_tilt = std::sin(ht.lighter_tilt);
  const auto lean = [&](const Vector v) {
    const double along_up = Dot(v, view.up),
                 along_forward = Dot(v, view.forward);
    return v - view.up * along_up - view.forward * along_forward +
           (view.up * cos_tilt + view.forward * sin_tilt) * along_up +
           (view.forward * cos_tilt - view.up * sin_tilt) * along_forward;
  };
  const Vector lighter_forward = Unit(lean(model_z)),
               lighter_up = Unit(lean(view.up));
  const Vector lighter_x = Unit(Cross(lighter_up, lighter_forward));
  pose.held = Frame(grip, lighter_forward,
                    lighter_up * std::cos(ht.lighter_roll) +
                        lighter_x * std::sin(ht.lighter_roll));
  const Vector up = {
      .x = pose.held.m[4], .y = pose.held.m[5], .z = pose.held.m[6]};
  const Vector z_axis = {
      .x = pose.held.m[8], .y = pose.held.m[9], .z = pose.held.m[10]};

  // The flame's size wobbles with a few unrelated sine waves, so it never
  // repeats visibly.
  const double flicker = 0.85 +
                         0.15 * std::sin(time * 23.0) * std::sin(time * 7.1) +
                         0.1 * std::sin(time * 31.0);
  pose.flame = Frame(grip + up * ht.flame_height + z_axis * ht.flame_shift,
                     view.forward, view.up, flicker);
}
}

BodyPose PoseBody(const BodyInput& in, const BodyParams& params,
                  const HeldParams& held) {
  BodyPose pose;
  const Vector view_fwd = DirOf(in.yaw, in.pitch);
  const Vector view_flat =
      Unit({.x = std::cos(in.yaw), .y = 0, .z = std::sin(in.yaw)});
  const Vector eye{.x = in.eye_x, .y = in.eye_height, .z = in.eye_z};

  BodyAxes body;
  body.feet = Vector{.x = in.eye_x, .y = 0, .z = in.eye_z} -
              view_flat * params.eye_ahead;
  body.chest = {.x = std::cos(in.body_yaw), .y = 0, .z = std::sin(in.body_yaw)};
  body.char_right = Cross(body.chest, kUp);
  pose.torso = Frame(body.feet, body.chest);
  pose.head = Frame(body.feet + kUp * params.neck_height, view_fwd);

  // Legs, and the arms against them, swing in proportion to how much the player
  // walks.
  const double swing = params.leg_swing * in.walk * std::sin(in.stride);
  PoseLegs(pose, body, swing, params);

  ViewAxes view;
  view.forward = view_fwd;
  view.right = Cross(view_flat, kUp);
  view.up = Unit(Cross(view.right, view_fwd), kUp);
  const Vector grip = HeldGrip(in, eye, view, held);
  pose.hand_x = grip.x;
  pose.hand_y = grip.y;
  pose.hand_z = grip.z;
  PoseArms(pose, body, grip, swing, in.raise, params);

  // A torch points where the beam goes; a lighter stands upright.
  if (in.lighter) {
    PoseLighter(pose, grip, view, held, in.time);
  } else {
    const Vector beam_dir = DirOf(
        in.yaw + in.hand_yaw,
        std::clamp(in.pitch + in.hand_pitch, -kMaxHeldPitch, kMaxHeldPitch));
    pose.held = Frame(grip, beam_dir);
  }
  return pose;
}

bool LoadBodyParams(json::Reader& r, const std::string& path, BodyParams& out) {
  out.neck_height = r.Number(path + ".neckHeight");
  out.eye_ahead = r.Number(path + ".eyeAhead");
  out.hip_height = r.Number(path + ".hipHeight");
  out.hip_half_spacing = r.Number(path + ".hipHalfSpacing");
  out.shoulder_height = r.Number(path + ".shoulderHeight");
  out.shoulder_half_spacing = r.Number(path + ".shoulderHalfSpacing");
  out.upper_arm = r.Number(path + ".upperArm");
  out.fore_arm = r.Number(path + ".foreArm");
  out.leg_swing = r.Number(path + ".legSwing");
  out.turn_limit = r.Number(path + ".turnLimit");
  out.raise_seconds = r.Number(path + ".raiseSeconds");
  if (r.Ok() && (out.upper_arm <= 0 || out.fore_arm <= 0 ||
                 out.raise_seconds <= 0 || out.turn_limit <= 0)) {
    r.Fail(path +
           ".upperArm, foreArm, raiseSeconds and turnLimit must be positive");
  }
  return r.Ok();
}

bool LoadHandSwayParams(json::Reader& r, const std::string& path,
                        HandSwayParams& out) {
  const struct {
    const char* key;
    double* member;
  } fields[] = {{.key = "springK", .member = &out.spring_k},
                {.key = "springDamp", .member = &out.spring_damp},
                {.key = "turnLag", .member = &out.turn_lag},
                {.key = "yawLimit", .member = &out.yaw_limit},
                {.key = "pitchLimit", .member = &out.pitch_limit},
                {.key = "walkSpeed", .member = &out.walk_speed},
                {.key = "walkEase", .member = &out.walk_ease},
                {.key = "strideRate", .member = &out.stride_rate},
                {.key = "walkYaw", .member = &out.walk_yaw},
                {.key = "walkPitch", .member = &out.walk_pitch},
                {.key = "walkSide", .member = &out.walk_side},
                {.key = "walkDown", .member = &out.walk_down},
                {.key = "idleYaw", .member = &out.idle_yaw},
                {.key = "idleBreath", .member = &out.idle_breath},
                {.key = "idleDrift", .member = &out.idle_drift},
                {.key = "idleSide", .member = &out.idle_side},
                {.key = "idleDown", .member = &out.idle_down},
                {.key = "tremorStill", .member = &out.tremor_still},
                {.key = "tremorWalk", .member = &out.tremor_walk},
                {.key = "bob", .member = &out.bob}};
  for (const auto& field : fields) {
    *field.member = r.Number(path + "." + field.key);
  }
  if (r.Ok() &&
      (out.spring_k <= 0 || out.spring_damp < 0 || out.walk_speed <= 0 ||
       out.yaw_limit < 0 || out.pitch_limit < 0)) {
    r.Fail(path +
           ".springK and walkSpeed must be positive; springDamp and the limits "
           "not negative");
  }
  return r.Ok();
}

HandPose HandSway::Update(const double dt, const double time, const double x,
                          const double z, const double view_yaw,
                          const double view_pitch,
                          const HandSwayParams& params) {
  const double step = std::max(dt, kMinStep);
  if (!primed_ || dt > kResetGap) {
    last_yaw_ = view_yaw;
    last_pitch_ = view_pitch;
    last_x_ = x;
    last_z_ = z;
    primed_ = true;
  }
  // Per-frame change of the view and the speed it implies, for the spring.
  const double turn_yaw = WrapPi(view_yaw - last_yaw_),
               turn_pitch = view_pitch - last_pitch_;
  const double speed = std::hypot(x - last_x_, z - last_z_) / step;
  last_yaw_ = view_yaw;
  last_pitch_ = view_pitch;
  last_x_ = x;
  last_z_ = z;

  // Spring-damper: turning the view pulls the beam the other way, then it
  // catches up.
  yaw_v_ += (-params.spring_k * yaw_ - params.spring_damp * yaw_v_) * step;
  yaw_ -= turn_yaw * params.turn_lag;
  pitch_v_ +=
      (-params.spring_k * pitch_ - params.spring_damp * pitch_v_) * step;
  pitch_ -= turn_pitch * params.turn_lag;
  yaw_ = std::clamp(yaw_ + yaw_v_ * step, -params.yaw_limit, params.yaw_limit);
  pitch_ = std::clamp(pitch_ + pitch_v_ * step, -params.pitch_limit,
                      params.pitch_limit);
  const double moving = std::min(1.0, speed / params.walk_speed);
  walk_ += (moving - walk_) * std::min(1.0, step * params.walk_ease);
  stride_ += speed * step * params.stride_rate;

  const double still = 1.0 - walk_;
  const double tremor = params.tremor_still + params.tremor_walk * walk_;
  // About one breath every 4.6 seconds.
  const double breath = std::sin(time * 1.35);
  const double drift = std::sin(time * 0.47) +
                       0.7 * std::sin(time * 0.83 + 1.7) +
                       0.4 * std::sin(time * 1.9 + 0.4);
  HandPose pose;
  pose.stride = stride_;
  pose.walk = walk_;
  pose.yaw = yaw_ + walk_ * params.walk_yaw * std::sin(stride_ * 0.5) +
             still * params.idle_yaw * drift +
             tremor * (std::sin(time * 1.7) + 0.6 * std::sin(time * 4.1 + 1.0));
  pose.pitch =
      pitch_ + walk_ * params.walk_pitch * std::sin(stride_ + 0.6) +
      still * (params.idle_breath * breath +
               params.idle_drift * (std::sin(time * 0.61 + 0.9) +
                                    0.6 * std::sin(time * 1.43))) +
      tremor * (std::sin(time * 1.3 + 2.0) + 0.6 * std::sin(time * 3.7));
  pose.side = walk_ * params.walk_side * std::sin(stride_ * 0.5) +
              still * params.idle_side * drift;
  pose.down =
      walk_ * params.walk_down * (0.5 - 0.5 * std::cos(stride_ * 2.0)) * 0.5 +
      params.bob * std::sin(time * 1.1) + still * params.idle_down * breath;
  return pose;
}

void BodyMotion::Update(const double dt, const double view_yaw,
                        const bool walking, const bool light_up,
                        const BodyParams& params) {
  if (!primed_) {
    yaw_ = view_yaw;
    primed_ = true;
  }
  yaw_ = TurnBody(yaw_, view_yaw, dt, walking, params);
  raise_ += ((light_up ? 1.0 : 0.0) - raise_) *
            std::min(1.0, dt / params.raise_seconds);
}

bool LoadHeldParams(json::Reader& r, const std::string& path, HeldParams& out) {
  out.right = r.Number(path + ".right");
  out.down = r.Number(path + ".down");
  out.forward = r.Number(path + ".forward");
  out.lighter_right = r.Number(path + ".lighterRight");
  out.lighter_down = r.Number(path + ".lighterDown");
  out.lighter_forward = r.Number(path + ".lighterForward");
  out.lighter_tilt = r.Number(path + ".lighterTilt");
  out.lighter_roll = r.Number(path + ".lighterRoll");
  out.lighter_turn = r.Number(path + ".lighterTurn");
  out.flame_height = r.Number(path + ".flameHeight");
  out.flame_shift = r.Number(path + ".flameShift");
  return r.Ok();
}
}
