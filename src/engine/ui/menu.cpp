#include "engine/ui/menu.h"

namespace mythos {
int MenuStep(int& index, const int count, const UiInput& in,
             const bool activate, const std::function<bool(int)>& usable) {
  const auto ok = [&](const int i) { return !usable || usable(i); };
  if (in.down_pressed) {
    do {
      index = (index + 1) % count;
    } while (!ok(index));
  }
  if (in.up_pressed) {
    do {
      index = (index + count - 1) % count;
    } while (!ok(index));
  }
  if (in.hover_item >= 0 && in.hover_item < count && ok(in.hover_item)) {
    index = in.hover_item;
  }
  const bool clicked_item =
      in.click_item >= 0 && in.click_item < count && ok(in.click_item);
  if (clicked_item) index = in.click_item;
  return (activate || clicked_item) ? index : -1;
}
}
