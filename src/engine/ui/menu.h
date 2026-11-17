#pragma once
// Menu input and list navigation, for any game's title menu, settings page and pause menu.
#include <functional>

namespace mythos {

// One frame of menu-ish intent. "Pressed" fields are true only on the frame of the press.
struct UiInput {
    bool confirmPressed = false;
    bool pausePressed = false;
    bool upPressed = false, downPressed = false;     // menu navigation
    bool leftPressed = false, rightPressed = false;  // menu value change
    // The free mouse on the menu pages, already hit-tested against the overlay's hotspots
    // (an item is a menu or settings row index; -1 = none).
    int hoverItem = -1;   // the pointer moved onto this item: select it
    int clickItem = -1;   // a click landed on this item: select and use it
    int clickValue = -1;  // ...on this value of it (a volume pip: 0..10), or -1
    int clickStep = 1;    // +1 for the left button, -1 for the right (steps a setting back)
    bool clicked = false; // a left click anywhere (continues from cards and documents)
};

// Moves the selection `index` through `count` items with up / down (wrapping, skipping the
// items `usable` rejects) and the pointer. Returns the item chosen this frame by `activate`
// (the game's confirm / interact key) or a click, or -1.
int menuStep(int& index, int count, const UiInput& in, bool activate, const std::function<bool(int)>& usable = {});

}  // namespace mythos
