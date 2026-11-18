#include "engine/platform/input.h"

#include <SDL3/SDL.h>

namespace mythos {

namespace {

// A key press (not an auto-repeat) turns on the matching one-frame flags.
void HandleKeyDown(SDL_Keycode key, UiInput& ui, ActionInput& act,
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
      break;  // menus handle it; the Exit item quits
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

// A click in menu mode: left steps forward (and counts as a click), right steps
// back.
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

// Held keys: the continuous movement state, re-read every frame.
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

}  // namespace

InputPoll PollInput(MoveInput& move, UiInput& ui, ActionInput& act,
                    bool relative_mouse, const PointAt& point_at) {
  InputPoll out;
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_EVENT_QUIT:
        out.quit = true;
        break;
      case SDL_EVENT_MOUSE_MOTION:
        if (relative_mouse) {  // looking around
          move.mouse_dx +=
              static_cast<decltype(move.mouse_dx)>(event.motion.xrel);
          move.mouse_dy +=
              static_cast<decltype(move.mouse_dy)>(event.motion.yrel);
        } else if (point_at) {  // hovering a menu
          ui.hover_item = point_at(static_cast<int>(event.motion.x),
                                   static_cast<int>(event.motion.y), nullptr);
        }
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (!relative_mouse) HandleMouseDown(event.button, ui, point_at);
        break;
      case SDL_EVENT_KEY_DOWN:
        if (!event.key.repeat) HandleKeyDown(event.key.key, ui, act, out);
        break;
      default:
        break;
    }
  }
  ReadHeldKeys(move, act);
  return out;
}

}  // namespace mythos
