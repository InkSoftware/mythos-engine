#ifndef MYTHOS_ENGINE_PLATFORM_INPUT_H_
#define MYTHOS_ENGINE_PLATFORM_INPUT_H_

#include <functional>

#include "engine/ui/menu.h"
#include "engine/world/player.h"

namespace mythos {
// What a poll found beyond the data written to the input structs.
struct InputPoll {
  bool quit = false;
  bool toggle_fullscreen = false;
};

// Maps a window position to a UI hotspot index (-1 for none) and optionally its
// value.
using PointAt = std::function<int(int x, int y, int* value)>;

// Drains the SDL event queue and reads held keys. Edge-triggered inputs
// (clicks, key presses) are set in `ui` and `act` and the caller clears them;
// mouse deltas accumulate into `move`. With `relative_mouse` the mouse looks
// around, otherwise it hovers and clicks UI items found through `point_at`.
InputPoll PollInput(MoveInput& move, UiInput& ui, ActionInput& act,
                    bool relative_mouse, const PointAt& point_at);
}

#endif
