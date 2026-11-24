#include "engine/ui/controls_ui.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "engine/data/i18n.h"

namespace mythos {
namespace {
constexpr float kHintMargin = 20.0f;

std::string Upper(std::string s) {
  for (char& c : s)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

// The name of an action for the player: "controls.<action>" when a table has
// it, else the action's own name.
std::string ActionLabel(const std::string& action) {
  const std::string key = "controls." + action;
  const std::string text = Tr(key);
  return text == key ? action : text;
}
}

std::string BindingLabel(const Binding& b) {
  switch (b.kind) {
    case BindKind::kKey:
      return b.code;
    case BindKind::kPadButton:
      return "PAD " + Upper(b.code);
    case BindKind::kPadAxis:
      return Upper(b.code) + (b.sign < 0 ? " -" : " +");
    case BindKind::kMouseButton:
      return "MOUSE " + Upper(b.code);
  }
  return b.code;
}

int ControlsRowCount(const InputMap& map, const std::string& context) {
  return static_cast<int>(map.Actions(context).size()) + 2;
}

ControlsResult UpdateControlsPage(InputMap& map, const std::string& context,
                                  ControlsState& state, const UiInput& ui,
                                  const std::optional<Binding>& pressed) {
  const std::vector<std::string> actions = map.Actions(context);
  const int count = static_cast<int>(actions.size());
  const int rows = count + 2;
  state.index = std::clamp(state.index, 0, rows - 1);
  state.slot =
      std::clamp(state.slot, 0, static_cast<int>(InputMap::kSlots) - 1);

  if (state.capturing) {
    if (ui.pause_pressed) {
      state.capturing = false;
      return ControlsResult::kNone;
    }
    if (!pressed) return ControlsResult::kNone;
    const std::string& action = actions[static_cast<size_t>(state.index)];
    state.note = map.FindConflict(action, *pressed);
    map.Rebind(action, static_cast<size_t>(state.slot), *pressed, true);
    state.capturing = false;
    return ControlsResult::kChanged;
  }

  if (ui.pause_pressed) return ControlsResult::kBack;
  if (ui.left_pressed) state.slot = 0;
  if (ui.right_pressed) {
    state.slot = static_cast<int>(InputMap::kSlots) - 1;
  }
  // A click on a binding picks its slot as well as the row.
  if (ui.clicked && ui.click_value > 0 && ui.click_item >= 0 &&
      ui.click_item < count) {
    state.slot = std::clamp(ui.click_value - 1, 0,
                            static_cast<int>(InputMap::kSlots) - 1);
  }
  const int chosen = MenuStep(state.index, rows, ui, ui.confirm_pressed);
  if (chosen < 0) return ControlsResult::kNone;
  state.note.clear();
  if (chosen < count) {
    state.capturing = true;  // the next input is the binding
    return ControlsResult::kNone;
  }
  if (chosen == count) {
    map.ResetAll();
    return ControlsResult::kChanged;
  }
  return ControlsResult::kBack;
}

void DrawControlsPage(UiCanvas& c, const MenuTheme& t, const InputMap& map,
                      const std::string& context, const ControlsState& state,
                      const double time_sec) {
  Surface& fb = c.fb;
  FillRect(fb, 0, 0, fb.w, fb.h, t.backdrop);
  const int px = (fb.w - t.page_w) / 2, py = (fb.h - t.page_h) / 2;
  const auto fpx = static_cast<float>(px), fpy = static_cast<float>(py),
             fw = static_cast<float>(fb.w), fh = static_cast<float>(fb.h);
  c.Text(t.heading, fw / 2.0f, fpy + t.title_y, Tr("controls.title"),
         Align::kCenter);

  Style row_style = t.item;
  row_style.size = t.row_size;
  const std::vector<std::string> actions = map.Actions(context);
  const int count = static_cast<int>(actions.size());
  const int rows = count + 2;
  float y = fpy + t.first_row_y;
  const float pitch =
      std::min(t.row_pitch,
               (fh - t.hint_from_bottom - 2 * kHintMargin - t.back_gap - y) /
                   static_cast<float>(std::max(rows - 1, 1)));
  const float left = fpx + t.margin,
              right = fpx + static_cast<float>(t.page_w) - t.margin;
  const float slot_w = (right - left) * 0.26f;
  for (int i = 0; i < rows; ++i) {
    if (i == count + 1) y += t.back_gap;
    const bool selected = state.index == i;
    Style st = row_style.Recolored(selected ? t.selected : t.text);
    if (selected) {
      st.fx = SelectionGlow(time_sec, 3.0f);
      c.Text(st, left - 5, y, "›", Align::kRight);
    }
    if (i < count) {
      const std::string& action = actions[static_cast<size_t>(i)];
      c.Text(st, left, y, ActionLabel(action));
      c.Hot(i, left - 12, y - 1, right - 2 * slot_w - 2, y + 10);
      const std::vector<Binding>& bindings = map.Bindings(action);
      for (int slot = 0; std::cmp_less(slot, InputMap::kSlots); ++slot) {
        const float x1 =
            right -
            static_cast<float>(static_cast<int>(InputMap::kSlots) - 1 - slot) *
                slot_w;
        const bool here = selected && state.slot == slot;
        std::string text = "-";
        if (here && state.capturing) {
          text = Tr("controls.press");
        } else if (static_cast<size_t>(slot) < bindings.size()) {
          text = BindingLabel(bindings[static_cast<size_t>(slot)]);
        }
        Style vs = row_style.Recolored(here ? t.selected
                                            : (selected ? t.text : t.dim));
        if (here) vs.fx = SelectionGlow(time_sec, 3.0f);
        c.Text(vs, x1, y, text, Align::kRight);
        c.Hot(i, x1 - slot_w + 2, y - 1, x1 + 2, y + 10, slot + 1);
      }
    } else {
      c.Text(st, left, y, Tr(i == count ? "controls.reset" : "settings.back"));
      c.Hot(i, left - 12, y - 1, right + 4, y + 10);
    }
    y += pitch;
  }
  if (!state.note.empty()) {
    c.Text(t.hint.Recolored(t.dim), fw / 2.0f, fh - t.hint_from_bottom - 11,
           Tr("controls.stolen") + " " + ActionLabel(state.note),
           Align::kCenter);
  }
  c.Text(t.hint, fw / 2.0f, fh - t.hint_from_bottom,
         Tr(state.capturing ? "controls.capture_hint" : "controls.hint"),
         Align::kCenter);
}
}
