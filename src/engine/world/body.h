#pragma once
// A first-person player's body as a mirror shows it, and the light in the hand: where each part stands this frame.
// Pure arithmetic (no GL): the renderer draws the matrices as model instances (see addBodyInstances in
// platform/gl/scene_renderer.h). The parts are the engine's own models: body_* (tools/mythos_assets.py), the torch
// and the lighter (tools/fetch_cc0_models.py) and the lighter's flame. The numbers come in as BodyParams and
// HeldParams (loadBodyParams, loadHeldParams read them from a game's tuning data).

#include <string>

#include "engine/core/json_reader.h"

namespace mythos {

// The figure's proportions (metres; the body_* models in mythos_assets.py).
struct BodyParams {
    double neckHeight = 1.50;      // where the head turns
    double eyeAhead = 0.10;        // the eye is this far in front of the neck
    double hipHeight = 0.88, hipHalfSpacing = 0.09;
    double shoulderHeight = 1.42, shoulderHalfSpacing = 0.22;
    double upperArm = 0.30, foreArm = 0.335;  // the arm models' lengths (the forearm includes the hand)
    double legSwing = 0.55;        // radians the legs swing from the hip at a walk
    double turnLimit = 0.9;        // how far the head may turn from the chest before the chest follows (radians)
    double raiseSeconds = 0.25;    // how long the arm takes to bring the light up
};

// Where the light is held in the view, relative to the eye (right, down, forward), and the lighter's pose and flame.
struct HeldParams {
    double right = 0.15, down = 0.16, forward = 0.42;
    double lighterRight = 0.15, lighterDown = 0.13, lighterForward = 0.26;
    double lighterTilt = 0.25;   // radians the lighter leans back
    double lighterRoll = 0.0;    // radians it is rolled about its own front axis, to stand it upright on the screen
    double lighterTurn = 0.35;  // radians it is turned about the view's up axis, to show its face
    double flameHeight = 0.026;  // the flame's base above the lighter's origin (its wick, in the lighter model)
    double flameShift = 0.010;   // and along the lighter's z axis
};

// Read from the objects at `path` ("body", "held"): every key of the struct, all required. Returns r.ok().
bool loadBodyParams(json::Reader& r, const std::string& path, BodyParams& out);
bool loadHeldParams(json::Reader& r, const std::string& path, HeldParams& out);

// Model-to-world, column-major (right, up, forward, position), the way the renderer takes it.
struct PartMatrix {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

struct BodyInput {
    double eyeX = 0, eyeZ = 0, eyeHeight = 1.55;  // where the camera is
    double yaw = 0, pitch = 0;                    // the view (yaw: direction (cos, sin) on the x, z plane)
    double bodyYaw = 0;                           // which way the chest faces (see turnBody)
    double stride = 0;                            // leg phase, radians
    double walk = 0;                              // 0 standing .. 1 walking
    double raise = 0;                             // 0 the arm hangs .. 1 the hand holds the light up
    bool lighter = false;                         // the light in the hand is a lighter, not a torch
    double handYaw = 0, handPitch = 0, handSide = 0, handDown = 0;  // the hand's sway (CameraState's)
    double time = 0;                              // seconds, for the flame
};

struct BodyPose {
    PartMatrix torso, head, legL, legR, upperArmL, foreArmL, upperArmR, foreArmR;
    PartMatrix held;   // the torch or the lighter, in the right hand
    PartMatrix flame;  // the lighter's flame
    double handX = 0, handY = 0, handZ = 0;  // where the right hand holds the light (world)
};

// The chest follows the view: it stays put while the head turns up to params.turnLimit, is dragged beyond it,
// and catches up at a walk. Returns the new chest yaw.
double turnBody(double bodyYaw, double viewYaw, double dt, bool walking, const BodyParams& t);

// Where everything stands. The body is behind the eye (params.eyeAhead), feet on the floor.
BodyPose poseBody(const BodyInput& in, const BodyParams& body, const HeldParams& held);

// Two-bone arm: the elbow between a shoulder and a hand target for arm segments of the given lengths. The target
// is pulled in when out of reach; the elbow bends towards `bend` (a direction, need not be unit or perpendicular).
struct ArmIk {
    double elbow[3];
    double hand[3];  // the target, clamped to the reach
};
ArmIk solveArm(const double shoulder[3], const double target[3], double upper, double fore, const double bend[3]);

}  // namespace mythos
