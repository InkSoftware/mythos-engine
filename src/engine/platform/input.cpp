#include "engine/platform/input.h"

#include <SDL3/SDL.h>

namespace mythos {

namespace {

// A key press (not an auto-repeat) turns on the matching one-frame flags.
void handleKeyDown(SDL_Keycode key, UiInput& ui, ActionInput& act, InputPoll& out) {
    switch (key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER: ui.confirmPressed = true; break;
        case SDLK_E:
        case SDLK_SPACE: act.interactPressed = true; break;
        case SDLK_F: act.flashlightPressed = true; break;
        case SDLK_TAB: act.notebookPressed = true; break;
        case SDLK_ESCAPE: ui.pausePressed = true; break;  // menus handle it; the Exit item quits
        case SDLK_W:
        case SDLK_UP: ui.upPressed = true; break;
        case SDLK_S:
        case SDLK_DOWN: ui.downPressed = true; break;
        case SDLK_A:
        case SDLK_LEFT: ui.leftPressed = true; break;
        case SDLK_D:
        case SDLK_RIGHT: ui.rightPressed = true; break;
        case SDLK_F11: out.toggleFullscreen = true; break;
        default: break;
    }
}

// A click in menu mode: left steps forward (and counts as a click), right steps back.
void handleMouseDown(const SDL_MouseButtonEvent& button, UiInput& ui, const PointAt& pointAt) {
    if (button.button != SDL_BUTTON_LEFT && button.button != SDL_BUTTON_RIGHT) return;
    const bool left = button.button == SDL_BUTTON_LEFT;
    if (pointAt) ui.clickItem = pointAt(static_cast<int>(button.x), static_cast<int>(button.y), &ui.clickValue);
    ui.clickStep = left ? 1 : -1;
    ui.clicked = ui.clicked || left;
}

// Held keys: the continuous movement state, re-read every frame.
void readHeldKeys(MoveInput& move, ActionInput& act) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    move.forward = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
    move.back = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
    move.turnLeft = keys[SDL_SCANCODE_LEFT];
    move.turnRight = keys[SDL_SCANCODE_RIGHT];
    move.strafeLeft = keys[SDL_SCANCODE_A];
    move.strafeRight = keys[SDL_SCANCODE_D];
    move.run = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    act.interactHeld = keys[SDL_SCANCODE_E] || keys[SDL_SCANCODE_SPACE];
}

}  // namespace

InputPoll pollInput(MoveInput& move, UiInput& ui, ActionInput& act, bool relativeMouse, const PointAt& pointAt) {
    InputPoll out;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                out.quit = true;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (relativeMouse) {  // looking around
                    move.mouseDx += static_cast<decltype(move.mouseDx)>(event.motion.xrel);
                    move.mouseDy += static_cast<decltype(move.mouseDy)>(event.motion.yrel);
                } else if (pointAt) {  // hovering a menu
                    ui.hoverItem = pointAt(static_cast<int>(event.motion.x), static_cast<int>(event.motion.y), nullptr);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (!relativeMouse) handleMouseDown(event.button, ui, pointAt);
                break;
            case SDL_EVENT_KEY_DOWN:
                if (!event.key.repeat) handleKeyDown(event.key.key, ui, act, out);
                break;
            default:
                break;
        }
    }
    readHeldKeys(move, act);
    return out;
}

}  // namespace mythos
