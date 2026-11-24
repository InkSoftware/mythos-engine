#ifndef MYTHOS_ENGINE_PLATFORM_INPUT_H_
#define MYTHOS_ENGINE_PLATFORM_INPUT_H_

#include <functional>
#include <optional>
#include <string>

#include "engine/platform/window.h"
#include "engine/ui/input_map.h"
#include "engine/ui/menu.h"
#include "engine/world/player.h"

namespace mythos {
// What the debug console was sent in a poll. `toggle` (the ` key or F12)
// is reported always, so a game can open the console; the rest only while
// `SetConsoleCapture` is on, and then `text` holds what was typed.
struct ConsoleInput {
  bool toggle = false;
  bool submit = false, backspace = false;
  bool up = false, down = false, complete = false;
  bool page_up = false, page_down = false;
  std::string text;
};

// What a poll found beyond the data written to the input structs.
struct InputPoll {
  bool quit = false;
  bool toggle_fullscreen = false;
  ConsoleInput console;
  // The key, mouse button or pad button pressed in this poll (the last one),
  // as a binding: what a "press a key" rebind prompt captures. Empty for none.
  std::optional<Binding> pressed;
};

// Turns on text input for the debug console: typed text and the editing keys
// go to `InputPoll::console`, and the game's own keys (movement, interact,
// menu) are ignored until it is turned off again.
void SetConsoleCapture(Window& window, bool capture);

// How hard `b` is held right now (0..1): a key, a mouse button or a pad button
// is 0 or 1, a pad axis its deflection past `dead_zone` on the bound side.
// The probe an `ActionTracker` takes.
double ProbeBinding(const Binding& b, double dead_zone = 0.2);

// Overwrites the movement and interact fields of a poll with the play actions
// of `actions` (see DefaultGameplayMap), so rebound keys drive the game. Mouse
// look and the other buttons stay as PollInput set them.
void ApplyActions(const ActionTracker& actions, MoveInput& move,
                  ActionInput& act);

// Maps a window position to a UI hotspot index (-1 for none) and optionally its
// value.
using PointAt = std::function<int(int x, int y, int* value)>;

// Drains the SDL event queue and reads held keys. Edge-triggered inputs
// (clicks, key presses) are set in `ui` and `act` and the caller clears them;
// mouse deltas accumulate into `move`. With `relative_mouse` the mouse looks
// around, otherwise it hovers and clicks UI items found through `point_at`.
// A game controller (the first one found, hot-pluggable) feeds the same
// structs: left stick = move keys, right stick = mouse look (when
// `relative_mouse`), face and shoulder buttons = the key presses (see
// input.cpp).
// Game controller extras (all do nothing without a controller). `Rumble` shakes
// the motors for `ms` (low and high frequency strengths 0..1), `Led` sets the
// light bar (a DualSense has one), `GamepadLastUsed` says whether the player's
// last input came from the controller (so a game can show its buttons rather
// than the keys).
void GamepadRumble(double low, double high, int ms);
void GamepadLed(int r, int g, int b);
bool GamepadConnected();
bool GamepadLastUsed();

InputPoll PollInput(MoveInput& move, UiInput& ui, ActionInput& act,
                    bool relative_mouse, const PointAt& point_at);
}

#endif
