#ifndef MYTHOS_ENGINE_UI_SETTINGS_UI_H_
#define MYTHOS_ENGINE_UI_SETTINGS_UI_H_

#include <string>

#include "engine/ui/settings.h"
#include "engine/ui/ui.h"

namespace mythos {
// Colors, fonts and layout numbers of the settings page, in overlay pixels, so
// a game can restyle it. The page is `page_w` by `page_h` centerd on the
// overlay.
struct MenuTheme {
  Style heading, item, hint;
  Style number;
  uint32_t text = 0xFFE6DCC8, dim = 0xFF8A807A, selected = 0xFFE8C45A;
  int page_w = 320, page_h = 200;
  uint32_t backdrop = 0xBE000000;

  float title_y = 4.0f;
  float first_row_y = 40.0f;
  float row_pitch = 12.0f;
  float back_gap = 6.0f;
  float margin = 44.0f;
  float row_size = 10.5f;
  float number_size = 9.5f;
  float hint_from_bottom = 14.0f;
  uint32_t pip_on = 0xFFD8CCB4, pip_off = 0x50D8CCB4;
};

// Draws one page of the settings menu over a dimmed backdrop and registers a
// hotspot per row (item = `SettingsItem`; value = pip 1..10 for a volume
// meter). `language_name` is the display name of the current language and
// `time_sec` animates the selection glow.
void DrawSettingsPage(UiCanvas& c, const MenuTheme& theme,
                      const GameSettings& s, SettingsPage page, int index,
                      const std::string& language_name, double time_sec);
}

#endif
