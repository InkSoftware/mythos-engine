#ifndef MYTHOS_ENGINE_WORLD_PLAYER_H_
#define MYTHOS_ENGINE_WORLD_PLAYER_H_

#include <optional>

#include "engine/core/json_reader.h"
#include "engine/core/math.h"
#include "engine/world/level.h"

// A first-person body on a grid level: look, turn, walk with collision and the
// footsteps that walking makes. Nothing here knows what game it serves.
namespace mythos {
// Pose of the player. Positions are in world units on the x, z plane.
struct Player {
  // Position; pos.y is world z.
  Vec2 pos;
  // Yaw in radians; the facing direction is (cos, sin) on the (x, z) plane.
  double angle = 0;

  // Look up (+) or down (-), clamped to +-PlayerParams::max_pitch.
  double pitch = 0;
  bool flashlight = false;
};

// One frame of movement intent.
struct MoveInput {
  bool forward = false, back = false;
  bool turn_left = false, turn_right = false;
  bool strafe_left = false, strafe_right = false;
  bool run = false;
  // An analog move (a game controller's stick), -1..1 with the dead zone
  // already taken out: forward is +, strafing right is +. Used only while no
  // movement key is held; the walker's speed follows the deflection.
  double analog_forward = 0.0;
  double analog_side = 0.0;
  // Mouse movement since the last frame, in pixels.
  double mouse_dx = 0.0;
  double mouse_dy = 0.0;
};

// One frame of the other buttons a first-person game takes.
struct ActionInput {
  bool interact_pressed = false;
  // Held down (for listening), as opposed to the press edge above.
  bool interact_held = false;
  bool flashlight_pressed = false;
  bool notebook_pressed = false;
};

// Tuning numbers for the player, loaded by LoadPlayerParams.
struct PlayerParams {
  // Collision radius in world units.
  double radius = 0.25;
  // Speeds in cells per second.
  double walk_speed = 0, run_speed = 0;
  // Radians per second with the turn keys.
  double turn_speed = 0;
  // Distance walked per footstep.
  double stride_length = 0;
  // Radians per pixel of mouse movement; max_pitch is in radians.
  double mouse_sensitivity = 0, max_pitch = 0;
};

// Reads the object at path (conventionally "player"): radius, walkSpeed,
// runSpeed, turnSpeed, strideLength, mouseSensitivity, maxPitch. Every key is
// required. Returns r.Ok().
bool LoadPlayerParams(json::Reader& r, const std::string& path,
                      PlayerParams& out);

// Mouse look: yaw follows mouse_dx, pitch follows mouse_dy (up is negative dy)
// and is clamped.
void ApplyLook(Player& p, const MoveInput& in, const PlayerParams& params);

// Walks and turns the player, counting distance for footsteps.
class PlayerWalker {
 public:
  // Turns and walks the player for dt seconds with collision. Returns the glyph
  // of the cell underfoot each time a full stride_length has been walked, else
  // nothing; the game maps that glyph to a footstep sound.
  std::optional<char> Update(const Level& level, Player& p, double dt,
                             const MoveInput& in, const PlayerParams& params);

  // Forgets the distance walked toward the next step.
  void Reset() { stride_accum_ = 0; }

 private:
  double stride_accum_ = 0;
};

// Something the view ray can rest on: a sphere at (at.x, height, at.y), with
// radius. Disabled ones are skipped.
struct AimCandidate {
  Vec2 at;
  double height = 0, radius = 0;
  bool enabled = true;
};

// Index of the nearest enabled candidate that the view ray from the player's
// eye hits within reach, with nothing solid between; -1 for none. Candidates
// stand on furniture or hang on walls, so the sight line is tested to a point
// 0.5 m short of them, on the player's side.
int FindAimed(const Level& level, const Player& p, double eye_height,
              double reach, const std::vector<AimCandidate>& candidates);
}

#endif
