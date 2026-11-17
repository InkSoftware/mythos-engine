#pragma once
// A first-person body on a grid level: look, turn, walk with collision, and the footsteps
// that walking makes. Nothing here knows what game it serves; the numbers come in as
// PlayerParams and the game turns a footstep into whatever sound it likes.
#include <optional>

#include "engine/core/json_reader.h"
#include "engine/world/level.h"
#include "engine/core/math.h"

namespace mythos {

struct Player {
    Vec2 pos;
    double angle = 0;  // yaw in radians; horizontal direction = (cos, sin) on the (x, y) map plane
    double pitch = 0;  // look up (+) or down (-), clamped to +-maxPitch
    bool flashlight = false;
};

// One frame of the player's movement intent.
struct MoveInput {
    bool forward = false, back = false;
    bool turnLeft = false, turnRight = false;
    bool strafeLeft = false, strafeRight = false;
    bool run = false;
    double mouseDx = 0.0;  // mouse movement since the last frame, in pixels
    double mouseDy = 0.0;
};

// One frame of the other buttons a first-person game takes.
struct ActionInput {
    bool interactPressed = false;
    bool interactHeld = false;  // held down (listening)
    bool flashlightPressed = false;
    bool notebookPressed = false;
};

struct PlayerParams {
    double radius = 0.25;
    double walkSpeed = 0, runSpeed = 0;  // cells per second
    double turnSpeed = 0;                // radians per second, with the turn keys
    double strideLength = 0;             // distance walked per footstep
    double mouseSensitivity = 0, maxPitch = 0;
};

// Reads the numbers from a JSON object at `path` ("player"): radius, walkSpeed, runSpeed,
// turnSpeed, strideLength, mouseSensitivity, maxPitch. Every key is required; returns r.ok().
bool loadPlayerParams(json::Reader& r, const std::string& path, PlayerParams& out);

// Mouse look: yaw follows dx, pitch follows dy (up is negative dy), clamped.
void applyLook(Player& p, const MoveInput& in, const PlayerParams& params);

// Walks and turns the player for dt seconds, one footstep per stride of actual movement.
class PlayerWalker {
public:
    // Returns the glyph of the floor cell underfoot when a stride has just completed.
    std::optional<char> update(const Level& level, Player& p, double dt, const MoveInput& in, const PlayerParams& params);
    void reset() { strideAccum_ = 0; }

private:
    double strideAccum_ = 0;
};

// What the view ray can rest on: a sphere at (at.x, height, at.y). Disabled ones are skipped.
struct AimCandidate {
    Vec2 at;
    double height = 0, radius = 0;
    bool enabled = true;
};

// The index of the nearest enabled candidate the view ray hits within `reach`, with nothing
// solid between the eye and it; -1 for none. Candidates stand on solid furniture or hang on
// walls, so the sight line stops just short of them, on the player's side.
int findAimed(const Level& level, const Player& p, double eyeHeight, double reach, const std::vector<AimCandidate>& candidates);

}  // namespace mythos
