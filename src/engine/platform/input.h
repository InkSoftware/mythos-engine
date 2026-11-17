#pragma once
// The keyboard and mouse as one frame of game input.
//
//   W / Up, S / Down   walk and menu up / down        A, D       strafe and menu left / right
//   Left, Right        turn                           Shift      run
//   E or Space         interact (held: listen)        F          flashlight
//   Tab                notebook                       Enter      confirm
//   Esc                pause / back                   F11        fullscreen
//   mouse              look while captured; hover and click on menus otherwise
#include <functional>

#include "engine/ui/menu.h"
#include "engine/world/player.h"

namespace mythos {

struct InputPoll {
    bool quit = false;              // the window was closed
    bool toggleFullscreen = false;  // F11 was pressed
};

// Maps a window position to the menu item (and value) drawn there; -1 for none.
using PointAt = std::function<int(int x, int y, int* value)>;

// Drains the SDL event queue and reads the key state into one frame of input. While
// `relativeMouse` the mouse moves the view; otherwise it hovers and clicks on `pointAt`'s items.
InputPoll pollInput(MoveInput& move, UiInput& ui, ActionInput& act, bool relativeMouse, const PointAt& pointAt);

}  // namespace mythos
