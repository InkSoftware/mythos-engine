#include "engine/platform/input.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <optional>

#include "engine/world/gamepad.h"

namespace mythos {
namespace {
bool g_console_capture = false;  // the debug console owns the keyboard

// Keys of the debug console. Returns true when the key was the console's (so
// the game does not see it): the toggle always, everything while it captures.
bool HandleConsoleKey(const SDL_Keycode key, const bool repeat,
                      ConsoleInput& console) {
  if ((key == SDLK_GRAVE || key == SDLK_F12) && !repeat) {
    console.toggle = true;
    return true;
  }
  if (!g_console_capture) return false;
  switch (key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      if (!repeat) console.submit = true;
      break;
    case SDLK_BACKSPACE:
      console.backspace = true;
      break;
    case SDLK_UP:
      console.up = true;
      break;
    case SDLK_DOWN:
      console.down = true;
      break;
    case SDLK_TAB:
      if (!repeat) console.complete = true;
      break;
    case SDLK_PAGEUP:
      console.page_up = true;
      break;
    case SDLK_PAGEDOWN:
      console.page_down = true;
      break;
    case SDLK_ESCAPE:
      if (!repeat) console.toggle = true;
      break;
    default:
      break;
  }
  return true;
}

// Key bindings for single presses. Movement keys also drive menu navigation.
void HandleKeyDown(const SDL_Keycode key, UiInput& ui, ActionInput& act,
                   InputPoll& out) {
  switch (key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      ui.confirm_pressed = true;
      break;
    case SDLK_E:
    case SDLK_SPACE:
      act.interact_pressed = true;
      break;
    case SDLK_F:
      act.flashlight_pressed = true;
      break;
    case SDLK_TAB:
      act.notebook_pressed = true;
      break;
    case SDLK_ESCAPE:
      ui.pause_pressed = true;
      break;
    case SDLK_W:
    case SDLK_UP:
      ui.up_pressed = true;
      break;
    case SDLK_S:
    case SDLK_DOWN:
      ui.down_pressed = true;
      break;
    case SDLK_A:
    case SDLK_LEFT:
      ui.left_pressed = true;
      break;
    case SDLK_D:
    case SDLK_RIGHT:
      ui.right_pressed = true;
      break;
    case SDLK_F11:
      out.toggle_fullscreen = true;
      break;
    default:
      break;
  }
}

// Left click activates (step +1), right click steps a value down (-1).
void HandleMouseDown(const SDL_MouseButtonEvent& button, UiInput& ui,
                     const PointAt& point_at) {
  if (button.button != SDL_BUTTON_LEFT && button.button != SDL_BUTTON_RIGHT) {
    return;
  }
  const bool left = button.button == SDL_BUTTON_LEFT;
  if (point_at) {
    ui.click_item = point_at(static_cast<int>(button.x),
                             static_cast<int>(button.y), &ui.click_value);
  }
  ui.click_step = left ? 1 : -1;
  ui.clicked = ui.clicked || left;
}

// ------------------------------------------------------------ game controller
// A DualSense (PS5), an Xbox pad or any other SDL gamepad. Left stick walks
// (and moves the menu cursor), right stick looks, D-pad moves the menu cursor.
// Cross/A uses (E), Circle/B pauses (Esc), Square/X is the torch (F),
// Triangle/Y the notebook (Tab), R1 confirms (Enter), Options pauses, the
// touchpad or Share opens the notebook, L3 or L2 runs.
constexpr double kStickMove = 0.45;    // a push this far walks or steps a menu
constexpr double kLookPixels = 900.0;  // pixels per second at full deflection
constexpr double kTriggerRun = 0.5;    // L2 pulled this far runs

SDL_Gamepad* g_pad = nullptr;  // the one in use (the first found)
bool g_pad_started = false;
bool g_pad_last_used = false;  // the last input was the pad's
Uint64 g_last_ns = 0;
bool g_stick_up = false, g_stick_down = false, g_stick_left = false,
     g_stick_right = false;  // for press edges in menus

void OpenFirstGamepad() {
  if (g_pad) return;
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  if (ids && count > 0) g_pad = SDL_OpenGamepad(ids[0]);
  SDL_free(ids);
}

void StartGamepads() {
  if (g_pad_started) return;
  g_pad_started = true;
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
    SDL_Log("gamepads unavailable: %s", SDL_GetError());
    return;
  }
  OpenFirstGamepad();
}

void HandlePadButton(const SDL_GamepadButton button, UiInput& ui,
                     ActionInput& act) {
  g_pad_last_used = true;
  switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
      act.interact_pressed = true;
      break;
    case SDL_GAMEPAD_BUTTON_EAST:
    case SDL_GAMEPAD_BUTTON_START:
      ui.pause_pressed = true;
      break;
    case SDL_GAMEPAD_BUTTON_WEST:
      act.flashlight_pressed = true;
      break;
    case SDL_GAMEPAD_BUTTON_NORTH:
    case SDL_GAMEPAD_BUTTON_BACK:
    case SDL_GAMEPAD_BUTTON_TOUCHPAD:
      act.notebook_pressed = true;
      break;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
      ui.confirm_pressed = true;
      break;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
      ui.up_pressed = true;
      break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
      ui.down_pressed = true;
      break;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
      ui.left_pressed = true;
      break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
      ui.right_pressed = true;
      break;
    default:
      break;
  }
}

double PadAxis(const SDL_GamepadAxis axis) {
  return StickAxisToUnit(SDL_GetGamepadAxis(g_pad, axis));
}

// Sticks and triggers, once per poll: held movement (OR-ed with the keys'),
// the right stick as mouse look, and the left stick's press edges for menus.
void ReadGamepad(MoveInput& move, UiInput& ui, ActionInput& act,
                 const bool relative_mouse, const double dt) {
  if (!g_pad) return;
  const double lx = StickDeadzone(PadAxis(SDL_GAMEPAD_AXIS_LEFTX));
  const double ly = StickDeadzone(PadAxis(SDL_GAMEPAD_AXIS_LEFTY));
  const double rx = StickDeadzone(PadAxis(SDL_GAMEPAD_AXIS_RIGHTX));
  const double ry = StickDeadzone(PadAxis(SDL_GAMEPAD_AXIS_RIGHTY));
  const bool up = ly<-kStickMove, down = ly> kStickMove;
  const bool left = lx<-kStickMove, right = lx> kStickMove;
  // The stick walks by its deflection (analog); the keys stay digital.
  move.analog_forward = -ly;
  move.analog_side = lx;
  if (std::fabs(lx) > 0 || std::fabs(ly) > 0 || std::fabs(rx) > 0 ||
      std::fabs(ry) > 0)
    g_pad_last_used = true;
  move.run = move.run ||
             SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_LEFT_STICK) ||
             PadAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kTriggerRun;
  act.interact_held = act.interact_held ||
                      SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_SOUTH);
  if (relative_mouse) {
    move.mouse_dx += StickLookPixels(rx, dt, kLookPixels);
    move.mouse_dy += StickLookPixels(ry, dt, kLookPixels);
  }
  ui.up_pressed = ui.up_pressed || (up && !g_stick_up);
  ui.down_pressed = ui.down_pressed || (down && !g_stick_down);
  ui.left_pressed = ui.left_pressed || (left && !g_stick_left);
  ui.right_pressed = ui.right_pressed || (right && !g_stick_right);
  g_stick_up = up;
  g_stick_down = down;
  g_stick_left = left;
  g_stick_right = right;
}

// Held-state controls are polled by scancode, so they follow the physical key
// position on any layout.
void ReadHeldKeys(MoveInput& move, ActionInput& act) {
  const bool* keys = SDL_GetKeyboardState(nullptr);
  move.forward = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
  move.back = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
  move.turn_left = keys[SDL_SCANCODE_LEFT];
  move.turn_right = keys[SDL_SCANCODE_RIGHT];
  move.strafe_left = keys[SDL_SCANCODE_A];
  move.strafe_right = keys[SDL_SCANCODE_D];
  move.run = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
  act.interact_held = keys[SDL_SCANCODE_E] || keys[SDL_SCANCODE_SPACE];
}
}

void SetConsoleCapture(Window& window, const bool capture) {
  if (capture == g_console_capture) return;
  g_console_capture = capture;
  if (capture) {
    SDL_StartTextInput(window.Sdl());
  } else {
    SDL_StopTextInput(window.Sdl());
  }
}

void GamepadRumble(const double low, const double high, const int ms) {
  if (!g_pad) return;
  const auto motor = [](const double v) {
    return static_cast<Uint16>(std::clamp(v, 0.0, 1.0) * 65535.0);
  };
  SDL_RumbleGamepad(g_pad, motor(low), motor(high),
                    static_cast<Uint32>(std::max(0, ms)));
}

void GamepadLed(const int r, const int g, const int b) {
  if (!g_pad) return;
  const auto byte = [](const int v) {
    return static_cast<Uint8>(std::clamp(v, 0, 255));
  };
  SDL_SetGamepadLED(g_pad, byte(r), byte(g), byte(b));
}

namespace {
constexpr struct {
  const char* name;
  int button;
} kMouseButtons[] = {{"Left", SDL_BUTTON_LEFT},
                     {"Middle", SDL_BUTTON_MIDDLE},
                     {"Right", SDL_BUTTON_RIGHT},
                     {"X1", SDL_BUTTON_X1},
                     {"X2", SDL_BUTTON_X2}};

std::optional<Binding> MouseBinding(const int button) {
  for (const auto& m : kMouseButtons) {
    if (m.button == button) return Binding{BindKind::kMouseButton, m.name, 0};
  }
  return std::nullopt;
}
}

double ProbeBinding(const Binding& b, const double dead_zone) {
  switch (b.kind) {
    case BindKind::kKey: {
      const SDL_Scancode sc = SDL_GetScancodeFromName(b.code.c_str());
      return sc != SDL_SCANCODE_UNKNOWN && SDL_GetKeyboardState(nullptr)[sc]
                 ? 1.0
                 : 0.0;
    }
    case BindKind::kMouseButton: {
      const SDL_MouseButtonFlags held = SDL_GetMouseState(nullptr, nullptr);
      for (const auto& m : kMouseButtons) {
        if (b.code == m.name) return (held & SDL_BUTTON_MASK(m.button)) ? 1 : 0;
      }
      return 0.0;
    }
    case BindKind::kPadButton: {
      const SDL_GamepadButton button =
          SDL_GetGamepadButtonFromString(b.code.c_str());
      return g_pad != nullptr && button != SDL_GAMEPAD_BUTTON_INVALID &&
                     SDL_GetGamepadButton(g_pad, button)
                 ? 1.0
                 : 0.0;
    }
    case BindKind::kPadAxis: {
      const SDL_GamepadAxis axis = SDL_GetGamepadAxisFromString(b.code.c_str());
      if (g_pad == nullptr || axis == SDL_GAMEPAD_AXIS_INVALID) return 0.0;
      return AxisStrength(b, PadAxis(axis), dead_zone);
    }
  }
  return 0.0;
}

void ApplyActions(const ActionTracker& actions, MoveInput& move,
                  ActionInput& act) {
  move.forward = actions.Down("forward");
  move.back = actions.Down("back");
  move.strafe_left = actions.Down("strafe_left");
  move.strafe_right = actions.Down("strafe_right");
  move.turn_left = actions.Down("turn_left");
  move.turn_right = actions.Down("turn_right");
  move.run = actions.Down("run");
  act.interact_pressed = actions.Pressed("interact");
  act.interact_held = actions.Down("interact");
}

bool GamepadConnected() { return g_pad != nullptr; }
bool GamepadLastUsed() { return g_pad != nullptr && g_pad_last_used; }

InputPoll PollInput(MoveInput& move, UiInput& ui, ActionInput& act,
                    const bool relative_mouse, const PointAt& point_at) {
  InputPoll out;
  StartGamepads();
  const Uint64 now_ns = SDL_GetTicksNS();
  const double dt =
      g_last_ns ? std::min(0.1, static_cast<double>(now_ns - g_last_ns) * 1e-9)
                : 0.0;
  g_last_ns = now_ns;
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_EVENT_QUIT:
        out.quit = true;
        break;
      case SDL_EVENT_MOUSE_MOTION:
        g_pad_last_used = false;
        if (relative_mouse) {
          move.mouse_dx +=
              static_cast<decltype(move.mouse_dx)>(event.motion.xrel);
          move.mouse_dy +=
              static_cast<decltype(move.mouse_dy)>(event.motion.yrel);
        } else if (point_at) {
          ui.hover_item = point_at(static_cast<int>(event.motion.x),
                                   static_cast<int>(event.motion.y), nullptr);
        }
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (const auto b = MouseBinding(event.button.button)) out.pressed = b;
        if (!relative_mouse) HandleMouseDown(event.button, ui, point_at);
        break;
      case SDL_EVENT_KEY_DOWN:
        g_pad_last_used = false;
        if (HandleConsoleKey(event.key.key, event.key.repeat, out.console)) {
          break;
        }
        if (!event.key.repeat) {
          HandleKeyDown(event.key.key, ui, act, out);
          if (const char* name = SDL_GetScancodeName(event.key.scancode);
              name != nullptr && *name != '\0') {
            out.pressed = Binding{BindKind::kKey, name, 0};
          }
        }
        break;
      case SDL_EVENT_TEXT_INPUT:
        if (g_console_capture) out.console.text += event.text.text;
        break;
      case SDL_EVENT_GAMEPAD_ADDED:
        OpenFirstGamepad();
        break;
      case SDL_EVENT_GAMEPAD_REMOVED:
        if (g_pad && SDL_GetGamepadID(g_pad) == event.gdevice.which) {
          SDL_CloseGamepad(g_pad);
          g_pad = nullptr;
          g_stick_up = g_stick_down = g_stick_left = g_stick_right = false;
          OpenFirstGamepad();  // another one may be plugged in
        }
        break;
      case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (const char* name = SDL_GetGamepadStringForButton(
                static_cast<SDL_GamepadButton>(event.gbutton.button))) {
          out.pressed = Binding{BindKind::kPadButton, name, 0};
        }
        HandlePadButton(static_cast<SDL_GamepadButton>(event.gbutton.button),
                        ui, act);
        break;
      default:
        break;
    }
  }
  if (g_console_capture) {
    // Typing must not walk the player.
    move.forward = move.back = move.strafe_left = move.strafe_right = false;
    move.turn_left = move.turn_right = move.run = false;
    act.interact_held = false;
  } else {
    ReadHeldKeys(move, act);
  }
  ReadGamepad(move, ui, act, relative_mouse, dt);
  return out;
}
}
