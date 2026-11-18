#ifndef MYTHOS_ENGINE_UI_MENU_H_
#define MYTHOS_ENGINE_UI_MENU_H_
// Menu input and list navigation, for any game's title menu, settings page and
// pause menu.
#include <functional>

namespace mythos {

// One frame of menu-ish intent. "Pressed" fields are true only on the frame of
// the press.
struct UiInput {
  bool confirm_pressed = false;
  bool pause_pressed = false;
  bool up_pressed = false, down_pressed = false;     // menu navigation
  bool left_pressed = false, right_pressed = false;  // menu value change
  // The free mouse on the menu pages, already hit-tested against the overlay's
  // hotspots (an item is a menu or settings row index; -1 = none).
  int hover_item = -1;   // the pointer moved onto this item: select it
  int click_item = -1;   // a click landed on this item: select and use it
  int click_value = -1;  // ...on this value of it (a volume pip: 0..10), or -1
  int click_step =
      1;  // +1 for the left button, -1 for the right (steps a setting back)
  bool clicked =
      false;  // a left click anywhere (continues from cards and documents)
};

// Moves the selection `index` through `count` items with up / down (wrapping,
// skipping the items `usable` rejects) and the pointer. Returns the item chosen
// this frame by `activate` (the game's confirm / interact key) or a click, or
// -1.
int MenuStep(int& index, int count, const UiInput& in, bool activate,
             const std::function<bool(int)>& usable = {});

}  // namespace mythos

#endif  // MYTHOS_ENGINE_UI_MENU_H_
