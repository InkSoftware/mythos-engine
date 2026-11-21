#ifndef MYTHOS_ENGINE_WORLD_BODY_H_
#define MYTHOS_ENGINE_WORLD_BODY_H_

#include <string>

#include "engine/core/json_reader.h"

// A first-person player's body as a mirror shows it, and the light in the hand:
// where each part stands this frame. Pure arithmetic (no GL); the renderer
// draws the matrices as model instances. Tuning comes in as BodyParams,
// HeldParams and HandSwayParams, read from a game's data.
namespace mythos {
// The figure's proportions and motion limits (meters, radians, seconds).
struct BodyParams {
  // Where the head turns.
  double neck_height = 1.50;
  // The eye is this far in front of the neck.
  double eye_ahead = 0.10;
  double hip_height = 0.88, hip_half_spacing = 0.09;
  double shoulder_height = 1.42, shoulder_half_spacing = 0.22;
  // Arm segment lengths (the forearm includes the hand).
  double upper_arm = 0.30, fore_arm = 0.335;
  // Radians the legs swing from the hip at a full walk.
  double leg_swing = 0.55;
  // How far the head may turn from the chest before the chest follows.
  double turn_limit = 0.9;

  // How long the arm takes to bring the light up.
  double raise_seconds = 0.25;
};

// Where the light is held, relative to the eye in the view (right, down,
// forward, in meters), and the lighter's pose and flame.
struct HeldParams {
  double right = 0.15, down = 0.16, forward = 0.42;
  double lighter_right = 0.15, lighter_down = 0.13, lighter_forward = 0.26;
  // Radians the lighter leans back.
  double lighter_tilt = 0.25;
  // Radians it is rolled about its own front axis, to stand it upright on
  // screen.
  double lighter_roll = 0.0;

  // Radians it is turned about the view's up axis, to show its face.
  double lighter_turn = 0.35;
  // The flame's base above the lighter's origin (its wick), and a shift along
  // the lighter's z axis.
  double flame_height = 0.026;

  double flame_shift = 0.010;
};

// Reads the object at path (conventionally "body"): every key of BodyParams is
// required; arm lengths, raiseSeconds and turnLimit must be positive. Returns
// r.Ok().
bool LoadBodyParams(json::Reader& r, const std::string& path, BodyParams& out);

// Reads the object at path (conventionally "held"): every key of HeldParams is
// required. Returns r.Ok().
bool LoadHeldParams(json::Reader& r, const std::string& path, HeldParams& out);

// Model-to-world transform, column-major (right, up, forward, position), as the
// renderer takes it. Defaults to identity.
struct PartMatrix {
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

// How the hand holding the light moves (seconds, radians, meters): a loose
// spring behind the view (turning drags the beam, which settles with a little
// overshoot), a walking sway, and a tremor and breath that never stop.
struct HandSwayParams {
  // Spring stiffness and damping of the beam.
  double spring_k = 70, spring_damp = 9;
  // How much of a turn the beam is left behind by.
  double turn_lag = 0.6;
  // The most the beam lags, in radians.
  double yaw_limit = 0.35, pitch_limit = 0.30;
  // Meters per second that count as walking at full strength.
  double walk_speed = 3.0;
  // How fast standing and walking blend (per second).
  double walk_ease = 6.0;
  // Stride radians per meter walked.
  double stride_rate = 3.4;
  // Sway amplitudes while walking.
  double walk_yaw = 0.022, walk_pitch = 0.018, walk_side = 0.018,
         walk_down = 0.025;
  // Sway amplitudes while standing still.
  double idle_yaw = 0.020, idle_breath = 0.012, idle_drift = 0.014,
         idle_side = 0.008, idle_down = 0.006;
  // The hand's shake, standing and walking (added together).
  double tremor_still = 0.004, tremor_walk = 0.003;
  // The hand's slow rise and fall, in meters.
  double bob = 0.004;
};

// Reads the object at path: every key of HandSwayParams is required; springK
// and walkSpeed must be positive and springDamp and the limits not negative.
// Returns r.Ok().
bool LoadHandSwayParams(json::Reader& r, const std::string& path,
                        HandSwayParams& out);

// The hand's pose this frame, in the terms of BodyInput's hand_* fields (yaw,
// pitch in radians, side and down in meters), plus the walk it came from: walk
// is 0 standing to 1 walking and stride the leg phase in radians.
struct HandPose {
  double yaw = 0, pitch = 0, side = 0, down = 0;
  double stride = 0, walk = 0;
};

// Follows the player's pose from frame to frame. Call Update every frame; a
// pause or a jump longer than a quarter of a second (a teleport, a new scene)
// restarts it from the current pose.
class HandSway {
 public:
  // Advances the spring by dt seconds. time is a monotonic clock in seconds
  // (drives the idle motion); x, z and view_yaw / view_pitch describe the
  // player this frame.
  HandPose Update(double dt, double time, double x, double z, double view_yaw,
                  double view_pitch, const HandSwayParams& p);

 private:
  double yaw_ = 0, pitch_ = 0, yaw_v_ = 0, pitch_v_ = 0;
  double last_yaw_ = 0, last_pitch_ = 0, last_x_ = 0, last_z_ = 0, stride_ = 0,
         walk_ = 0;
  bool primed_ = false;
};

// The chest's facing (see TurnBody) and how far the arm has brought the light
// up, eased frame by frame.
class BodyMotion {
 public:
  // Advances by dt seconds. Read Yaw() and Raise() afterwards. light_up is true
  // while the player holds the light up.
  void Update(double dt, double view_yaw, bool walking, bool light_up,
              const BodyParams& params);

  // Chest yaw in radians.
  [[nodiscard]] double Yaw() const { return yaw_; }
  // 0 the arm hangs to 1 the hand holds the light up.
  [[nodiscard]] double Raise() const { return raise_; }

 private:
  double yaw_ = 0, raise_ = 0;
  bool primed_ = false;
};

// Everything PoseBody needs about the player this frame.
struct BodyInput {
  // Where the camera is (world units).
  double eye_x = 0, eye_z = 0, eye_height = 1.55;
  // The view; yaw is the direction (cos, sin) on the x, z plane.
  double yaw = 0, pitch = 0;
  // Which way the chest faces (see TurnBody).
  double body_yaw = 0;
  // Leg phase in radians.
  double stride = 0;
  // 0 standing to 1 walking.
  double walk = 0;
  // 0 the arm hangs to 1 the hand holds the light up.
  double raise = 0;
  // The light in the hand is a lighter, not a torch.
  bool lighter = false;
  // The hand's sway, from HandPose.
  double hand_yaw = 0, hand_pitch = 0, hand_side = 0, hand_down = 0;
  // Seconds, for the flame's flicker.
  double time = 0;
};

// Where every part stands: model-to-world matrices, plus the world position of
// the right hand's grip.
struct BodyPose {
  PartMatrix torso, head, leg_l, leg_r, upper_arm_l, fore_arm_l, upper_arm_r,
      fore_arm_r;
  // The torch or the lighter, in the right hand.
  PartMatrix held;
  // The lighter's flame (meaningful only when BodyInput::lighter).
  PartMatrix flame;
  double hand_x = 0, hand_y = 0, hand_z = 0;
};

// The chest follows the view: it stays put while the head turns up to
// params.turn_limit, is dragged beyond it, and catches up at a walk. Returns
// the new chest yaw in radians.
double TurnBody(double body_yaw, double view_yaw, double dt, bool walking,
                const BodyParams& params);

// Poses the whole figure. The body stands behind the eye (params.eye_ahead)
// with its feet on the floor; the right hand reaches for the grip.
BodyPose PoseBody(const BodyInput& in, const BodyParams& params,
                  const HeldParams& held);

// Result of SolveArm: joint positions as {x, y, z}.
struct ArmIk {
  double elbow[3];
  // The target, clamped to the reach.
  double hand[3];
};

// Two-bone arm: the elbow between a shoulder and a hand target, for segments of
// lengths upper and fore. The target is pulled in when out of reach; the elbow
// bends toward bend (a direction; it need be neither unit nor perpendicular to
// the arm).
ArmIk SolveArm(const double shoulder[3], const double target[3], double upper,
               double fore, const double bend[3]);
}

#endif
