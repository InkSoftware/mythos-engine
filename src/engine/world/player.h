#ifndef MYTHOS_ENGINE_WORLD_PLAYER_H_
#define MYTHOS_ENGINE_WORLD_PLAYER_H_
// A first-person body on a grid level: look, turn, walk with collision, and the
// footsteps that walking makes. Nothing here knows what game it serves; the
// numbers come in as PlayerParams and the game turns a footstep into whatever
// sound it likes.
#include <optional>

#include "engine/core/json_reader.h"
#include "engine/core/math.h"
#include "engine/world/level.h"

namespace mythos {

struct Player {
  Vec2 pos;
  double angle = 0;  // yaw in radians; horizontal direction = (cos, sin) on the
                     // (x, y) map plane
  double pitch = 0;  // look up (+) or down (-), clamped to +-maxPitch
  bool flashlight = false;
};

// One frame of the player's movement intent.
struct MoveInput {
  bool forward = false, back = false;
  bool turn_left = false, turn_right = false;
  bool strafe_left = false, strafe_right = false;
  bool run = false;
  double mouse_dx = 0.0;  // mouse movement since the last frame, in pixels
  double mouse_dy = 0.0;
};

// One frame of the other buttons a first-person game takes.
struct ActionInput {
  bool interact_pressed = false;
  bool interact_held = false;  // held down (listening)
  bool flashlight_pressed = false;
  bool notebook_pressed = false;
};

struct PlayerParams {
  double radius = 0.25;
  double walk_speed = 0, run_speed = 0;  // cells per second
  double turn_speed = 0;     // radians per second, with the turn keys
  double stride_length = 0;  // distance walked per footstep
  double mouse_sensitivity = 0, max_pitch = 0;
};

// Reads the numbers from a JSON object at `path` ("player"): radius, walkSpeed,
// runSpeed, turnSpeed, strideLength, mouseSensitivity, maxPitch. Every key is
// required; returns r.ok().
bool LoadPlayerParams(json::Reader& r, const std::string& path,
                      PlayerParams& out);

// Mouse look: yaw follows dx, pitch follows dy (up is negative dy), clamped.
void ApplyLook(Player& p, const MoveInput& in, const PlayerParams& params);

// Walks and turns the player for dt seconds, one footstep per stride of actual
// movement.
class PlayerWalker {
 public:
  // Returns the glyph of the floor cell underfoot when a stride has just
  // completed.
  std::optional<char> Update(const Level& level, Player& p, double dt,
                             const MoveInput& in, const PlayerParams& params);
  void Reset() { stride_accum_ = 0; }

 private:
  double stride_accum_ = 0;
};

// What the view ray can rest on: a sphere at (at.x, height, at.y). Disabled
// ones are skipped.
struct AimCandidate {
  Vec2 at;
  double height = 0, radius = 0;
  bool enabled = true;
};

// The index of the nearest enabled candidate the view ray hits within `reach`,
// with nothing solid between the eye and it; -1 for none. Candidates stand on
// solid furniture or hang on walls, so the sight line stops just short of them,
// on the player's side.
int FindAimed(const Level& level, const Player& p, double eye_height,
              double reach, const std::vector<AimCandidate>& candidates);

}  // namespace mythos

#endif  // MYTHOS_ENGINE_WORLD_PLAYER_H_
