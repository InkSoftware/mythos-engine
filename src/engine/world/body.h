#ifndef MYTHOS_ENGINE_WORLD_BODY_H_
#define MYTHOS_ENGINE_WORLD_BODY_H_
// A first-person player's body as a mirror shows it, and the light in the hand:
// where each part stands this frame. Pure arithmetic (no GL): the renderer
// draws the matrices as model instances (see addBodyInstances in
// platform/gl/scene_renderer.h). The parts are the engine's own models: body_*
// (tools/mythos_assets.py), the torch and the lighter
// (tools/fetch_cc0_models.py) and the lighter's flame. The numbers come in as
// BodyParams and HeldParams (loadBodyParams, loadHeldParams read them from a
// game's tuning data).

#include <string>

#include "engine/core/json_reader.h"

namespace mythos {

// The figure's proportions (metres; the body_* models in mythos_assets.py).
struct BodyParams {
  double neck_height = 1.50;  // where the head turns
  double eye_ahead = 0.10;    // the eye is this far in front of the neck
  double hip_height = 0.88, hip_half_spacing = 0.09;
  double shoulder_height = 1.42, shoulder_half_spacing = 0.22;
  double upper_arm = 0.30,
         fore_arm =
             0.335;  // the arm models' lengths (the forearm includes the hand)
  double leg_swing = 0.55;  // radians the legs swing from the hip at a walk
  double turn_limit = 0.9;  // how far the head may turn from the chest before
                            // the chest follows (radians)
  double raise_seconds = 0.25;  // how long the arm takes to bring the light up
};

// Where the light is held in the view, relative to the eye (right, down,
// forward), and the lighter's pose and flame.
struct HeldParams {
  double right = 0.15, down = 0.16, forward = 0.42;
  double lighter_right = 0.15, lighter_down = 0.13, lighter_forward = 0.26;
  double lighter_tilt = 0.25;  // radians the lighter leans back
  double lighter_roll = 0.0;   // radians it is rolled about its own front axis,
                               // to stand it upright on the screen
  double lighter_turn =
      0.35;  // radians it is turned about the view's up axis, to show its face
  double flame_height = 0.026;  // the flame's base above the lighter's origin
                                // (its wick, in the lighter model)
  double flame_shift = 0.010;   // and along the lighter's z axis
};

// Read from the objects at `path` ("body", "held"): every key of the struct,
// all required. Returns r.ok().
bool LoadBodyParams(json::Reader& r, const std::string& path, BodyParams& out);
bool LoadHeldParams(json::Reader& r, const std::string& path, HeldParams& out);

// Model-to-world, column-major (right, up, forward, position), the way the
// renderer takes it.
struct PartMatrix {
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

// How the hand that holds the light moves, as seconds, radians and metres: a
// loose spring behind the view (turning drags the beam, which settles with a
// little overshoot), a walking sway, and a tremor and breath that never stop.
struct HandSwayParams {
  double spring_k = 70, spring_damp = 9;  // the beam's spring and its damping
  double turn_lag = 0.6;  // how much of a turn the beam is left behind by
  double yaw_limit = 0.35,
         pitch_limit = 0.30;  // the most the beam lags (radians)
  double walk_speed = 3.0;    // metres a second that count as walking at full
  double walk_ease = 6.0;    // how fast standing and walking blend (per second)
  double stride_rate = 3.4;  // stride radians per metre walked
  double walk_yaw = 0.022, walk_pitch = 0.018, walk_side = 0.018,
         walk_down = 0.025;  // the sway at a walk
  double idle_yaw = 0.020, idle_breath = 0.012, idle_drift = 0.014,
         idle_side = 0.008, idle_down = 0.006;  // standing still
  double tremor_still = 0.004,
         tremor_walk = 0.003;  // the hand's shake, standing and walking (added)
  double bob = 0.004;          // the hand's slow rise and fall (metres)
};
bool LoadHandSwayParams(json::Reader& r, const std::string& path,
                        HandSwayParams& out);

// The hand's pose this frame, in CameraState's terms (handYaw, handPitch,
// handSide, handDown), with the walk it came from: `walk` 0 standing .. 1
// walking, and `stride` the leg phase in radians (BodyInput's, before any
// scaling).
struct HandPose {
  double yaw = 0, pitch = 0, side = 0, down = 0;
  double stride = 0, walk = 0;
};

// Follows the player's pose from frame to frame. Call update every frame; a
// pause or a jump longer than a quarter of a second (a teleport, a new scene)
// restarts it from the current pose.
class HandSway {
 public:
  HandPose Update(double dt, double time, double x, double z, double view_yaw,
                  double view_pitch, const HandSwayParams& p);

 private:
  double yaw_ = 0, pitch_ = 0, yaw_v_ = 0, pitch_v_ = 0;
  double last_yaw_ = 0, last_pitch_ = 0, last_x_ = 0, last_z_ = 0, stride_ = 0,
         walk_ = 0;
  bool primed_ = false;
};

// The chest's facing (turnBody) and how far the arm has brought the light up,
// eased frame by frame.
class BodyMotion {
 public:
  // Returns nothing; read yaw() and raise() afterwards. `lightUp`: the player
  // holds the light up.
  void Update(double dt, double view_yaw, bool walking, bool light_up,
              const BodyParams& params);
  double Yaw() const { return yaw_; }
  double Raise() const { return raise_; }

 private:
  double yaw_ = 0, raise_ = 0;
  bool primed_ = false;
};

struct BodyInput {
  double eye_x = 0, eye_z = 0, eye_height = 1.55;  // where the camera is
  double yaw = 0,
         pitch = 0;  // the view (yaw: direction (cos, sin) on the x, z plane)
  double body_yaw = 0;   // which way the chest faces (see turnBody)
  double stride = 0;     // leg phase, radians
  double walk = 0;       // 0 standing .. 1 walking
  double raise = 0;      // 0 the arm hangs .. 1 the hand holds the light up
  bool lighter = false;  // the light in the hand is a lighter, not a torch
  double hand_yaw = 0, hand_pitch = 0, hand_side = 0,
         hand_down = 0;  // the hand's sway (CameraState's)
  double time = 0;       // seconds, for the flame
};

struct BodyPose {
  PartMatrix torso, head, leg_l, leg_r, upper_arm_l, fore_arm_l, upper_arm_r,
      fore_arm_r;
  PartMatrix held;   // the torch or the lighter, in the right hand
  PartMatrix flame;  // the lighter's flame
  double hand_x = 0, hand_y = 0,
         hand_z = 0;  // where the right hand holds the light (world)
};

// The chest follows the view: it stays put while the head turns up to
// params.turnLimit, is dragged beyond it, and catches up at a walk. Returns the
// new chest yaw.
double TurnBody(double body_yaw, double view_yaw, double dt, bool walking,
                const BodyParams& params);

// Where everything stands. The body is behind the eye (params.eyeAhead), feet
// on the floor.
BodyPose PoseBody(const BodyInput& in, const BodyParams& params,
                  const HeldParams& held);

// Two-bone arm: the elbow between a shoulder and a hand target for arm segments
// of the given lengths. The target is pulled in when out of reach; the elbow
// bends towards `bend` (a direction, need not be unit or perpendicular).
struct ArmIk {
  double elbow[3];
  double hand[3];  // the target, clamped to the reach
};
ArmIk SolveArm(const double shoulder[3], const double target[3], double upper,
               double fore, const double bend[3]);

}  // namespace mythos

#endif  // MYTHOS_ENGINE_WORLD_BODY_H_
