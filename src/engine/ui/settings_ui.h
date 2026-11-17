#pragma once
// Draws the settings pages (see settings.h) on a UiCanvas, with the game's own type styles and
// colours. The rows' texts come from the engine's built-in strings, which a game may override.
#include <string>

#include "engine/ui/settings.h"
#include "engine/ui/ui.h"

namespace mythos {

// How a menu page looks: the game's styles, colours and the centred area the page is laid out in.
struct MenuTheme {
    Style heading, item, hint;
    Style number;  // lining figures, for the window size
    uint32_t text = 0xFFE6DCC8, dim = 0xFF8A807A, selected = 0xFFE8C45A;
    int pageW = 320, pageH = 200;
    uint32_t backdrop = 0xBE000000;  // over the scene
    // Layout, in overlay pixels, inside the page area.
    float titleY = 4.0f;       // top of the page title
    float firstRowY = 40.0f;   // top of the first row
    float rowPitch = 12.0f;    // from one row to the next
    float backGap = 6.0f;      // extra space above Back
    float margin = 44.0f;      // from the page's sides to the rows' text
    float rowSize = 10.5f;     // type size of a row
    float numberSize = 9.5f;   // type size of the window size's figures
    float hintFromBottom = 14.0f;
    uint32_t pipOn = 0xFFD8CCB4, pipOff = 0x50D8CCB4;  // volume pips: lit and unlit
};

// One settings page: its title, its rows (each a clickable hotspot whose item is the row's
// SettingsItem) and the hint. languageName is shown on the language row.
void drawSettingsPage(UiCanvas& c, const MenuTheme& theme, const GameSettings& s, SettingsPage page, int index,
                      const std::string& languageName, double timeSec);

}  // namespace mythos
