// The controls page: lists the actions of an InputMap context with their two
// bindings, and lets the player rebind one ("press a key"), reset them all or
// go back. The rules (`UpdateControlsPage`) are headless and tested; the
// drawing (`DrawControlsPage`) uses the same canvas and `MenuTheme` as the
// settings page. A game keeps a `ControlsState`, feeds it each frame, saves
// the map when it reports a change (InputMap::ToJson), and shows it where it
// shows the settings.
#ifndef MYTHOS_ENGINE_UI_CONTROLS_UI_H_
#define MYTHOS_ENGINE_UI_CONTROLS_UI_H_

#include <optional>
#include <string>

#include "engine/ui/input_map.h"
#include "engine/ui/menu.h"
#include "engine/ui/settings_ui.h"

namespace mythos {
struct ControlsState {
  // The selected row: 0..actions-1, then "reset", then "back".
  int index = 0;
  // The slot (0 or 1) that a rebind or an unbind goes to.
  int slot = 0;
  // Waiting for the next key, button or stick push.
  bool capturing = false;
  // The action another one was stolen from in the last rebind (shown once).
  std::string note;
};

enum class ControlsResult {
  kNone,
  // The map changed: save it.
  kChanged,
  // Leave the page.
  kBack,
};

// A binding as a player reads it: "W", "Left Shift", "PAD SOUTH", "LEFTX -",
// "MOUSE LEFT".
std::string BindingLabel(const Binding& b);

// Steps the page one frame. `ui` is the menu input (up / down move, left /
// right pick the slot, confirm starts a rebind or presses reset / back, pause
// goes back or cancels a rebind; a click on a row selects it, a second click
// activates, a click on a slot picks it). `pressed` is what PollInput saw
// (`InputPoll::pressed`); while capturing it becomes the binding, taking the
// input from another action in the same context. Delete on the capture
// unbinds is not offered: unbind by rebinding.
ControlsResult UpdateControlsPage(InputMap& map, const std::string& context,
                                  ControlsState& state, const UiInput& ui,
                                  const std::optional<Binding>& pressed);

// The row count of the page for `context`: actions plus reset and back.
int ControlsRowCount(const InputMap& map, const std::string& context);

void DrawControlsPage(UiCanvas& c, const MenuTheme& theme, const InputMap& map,
                      const std::string& context, const ControlsState& state,
                      double time_sec);
}

#endif
