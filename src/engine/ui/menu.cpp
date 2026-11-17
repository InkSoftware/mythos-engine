#include "engine/ui/menu.h"

namespace mythos {

int menuStep(int& index, int count, const UiInput& in, bool activate, const std::function<bool(int)>& usable) {
    auto ok = [&](int i) { return !usable || usable(i); };
    if (in.downPressed) {
        do index = (index + 1) % count;
        while (!ok(index));
    }
    if (in.upPressed) {
        do index = (index + count - 1) % count;
        while (!ok(index));
    }
    if (in.hoverItem >= 0 && in.hoverItem < count && ok(in.hoverItem)) index = in.hoverItem;
    const bool clickedItem = in.clickItem >= 0 && in.clickItem < count && ok(in.clickItem);
    if (clickedItem) index = in.clickItem;
    return (activate || clickedItem) ? index : -1;
}

}  // namespace mythos
