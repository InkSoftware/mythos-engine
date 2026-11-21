#ifndef MYTHOS_ENGINE_UI_MENU_H_
#define MYTHOS_ENGINE_UI_MENU_H_

#include <functional>

namespace mythos {
// One frame of menu input, already reduced from keyboard, pad and mouse. The
// `*_pressed` flags are edge-triggered. `hover_item` is the hotspot under the
// pointer and `click_item` the one clicked this frame (-1 for none);
// `click_value` is the sub-item of that hotspot (such as a volume pip) and
// `click_step` the direction a click moves a value (+1 or -1). `clicked` is set
// when any click happened.
struct UiInput {
  bool confirm_pressed = false;
  bool pause_pressed = false;
  bool up_pressed = false, down_pressed = false;
  bool left_pressed = false, right_pressed = false;

  int hover_item = -1;
  int click_item = -1;
  int click_value = -1;
  int click_step = 1;
  bool clicked = false;
};

// Moves a menu selection. Up and down wrap around and skip items for which
// `usable` is false (all are usable when it is empty); hovering or clicking a
// usable item selects it. `index` is updated in place. Returns the chosen item
// when `activate` is set or an item was clicked, otherwise -1. Do not call with
// `count` of 0 or with no usable item.
int MenuStep(int& index, int count, const UiInput& in, bool activate,
             const std::function<bool(int)>& usable = {});
}

#endif
