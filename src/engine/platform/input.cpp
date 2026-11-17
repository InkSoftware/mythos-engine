#include "engine/platform/input.h"

#include <SDL3/SDL.h>

namespace mythos {

InputPoll pollInput(MoveInput& move, UiInput& ui, ActionInput& act, bool relativeMouse, const PointAt& pointAt) {
    InputPoll out;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_EVENT_QUIT:
                out.quit = true;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (relativeMouse) {
                    move.mouseDx += static_cast<decltype(move.mouseDx)>(e.motion.xrel);
                    move.mouseDy += static_cast<decltype(move.mouseDy)>(e.motion.yrel);
                } else if (pointAt) {
                    ui.hoverItem = pointAt(static_cast<int>(e.motion.x), static_cast<int>(e.motion.y), nullptr);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (relativeMouse) break;
                if (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT) {
                    const bool left = e.button.button == SDL_BUTTON_LEFT;
                    if (pointAt) ui.clickItem = pointAt(static_cast<int>(e.button.x), static_cast<int>(e.button.y), &ui.clickValue);
                    ui.clickStep = left ? 1 : -1;
                    ui.clicked = ui.clicked || left;
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.repeat) break;
                switch (e.key.key) {
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
                break;
            default:
                break;
        }
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    move.forward = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
    move.back = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
    move.turnLeft = keys[SDL_SCANCODE_LEFT];
    move.turnRight = keys[SDL_SCANCODE_RIGHT];
    move.strafeLeft = keys[SDL_SCANCODE_A];
    move.strafeRight = keys[SDL_SCANCODE_D];
    move.run = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    act.interactHeld = keys[SDL_SCANCODE_E] || keys[SDL_SCANCODE_SPACE];
    return out;
}

}  // namespace mythos
