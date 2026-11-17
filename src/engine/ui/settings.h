#pragma once
// Player options and the settings page that edits them: window size, fullscreen, vsync, frame
// rate limit, render scale, anti-aliasing, shadows, film grain, motion blur, camera effect, three volumes and
// the language. The platform layer applies them and keeps them in
// a key=value file (toKeyValues / applyKeyValues).
#include <map>
#include <string>
#include <vector>

#include "engine/data/i18n.h"
#include "engine/ui/menu.h"

namespace mythos {

// Window sizes offered on the settings page (windowed mode; fullscreen uses the desktop).
struct Resolution {
    int w, h;
};
inline constexpr Resolution kResolutions[] = {{960, 600},   {1280, 720},  {1280, 800},  {1366, 768},
                                              {1600, 900},  {1680, 1050}, {1920, 1080}, {1920, 1200},
                                              {2560, 1440}, {3840, 2160}};
inline constexpr int kResolutionCount = static_cast<int>(sizeof(kResolutions) / sizeof(kResolutions[0]));

// The index of w x h in kResolutions, or of the default (960x600) if it is not listed.
int resolutionIndex(int w, int h);

// Frame rate limits offered on the settings page (0 = none: vsync, if on, still paces the game).
inline constexpr int kFpsCaps[] = {0, 30, 60, 90, 120, 144, 240};
inline constexpr int kFpsCapCount = static_cast<int>(sizeof(kFpsCaps) / sizeof(kFpsCaps[0]));

// Render scale presets: the height in lines of the internal picture (its width follows the
// window's aspect). The last ones are for high-end systems.
inline constexpr int kRenderScaleLines[] = {300, 600, 900, 1080, 1440, 2160};
inline constexpr int kRenderScaleCount = static_cast<int>(sizeof(kRenderScaleLines) / sizeof(kRenderScaleLines[0]));

// Shadow map levels: 0 off, then very low, low, medium, max, ultra max (see shadowLevel in
// the renderer: more lamps cast shadows, from bigger maps, at each step).
inline constexpr int kShadowLevelCount = 6;

// Motion blur strengths in percent (0 = off), as offered on the settings page.
inline constexpr int kMotionBlurScales[] = {0, 25, 50, 75, 100, 150, 200};
inline constexpr int kMotionBlurCount = static_cast<int>(sizeof(kMotionBlurScales) / sizeof(kMotionBlurScales[0]));

// Anti-aliasing modes: off, FXAA (a cheap filter), then multisampling (MSAA) at 2x, 4x and 8x.
inline constexpr int kAntiAliasCount = 5;
// The samples of each mode: none for off and FXAA (a filter on the finished picture).
inline constexpr int kAntiAliasSamples[kAntiAliasCount] = {0, 0, 2, 4, 8};

struct GameSettings {
    int resolution = 0;       // index into kResolutions
    int renderScale = 1;      // index into kRenderScaleLines
    int antiAlias = 1;        // 0..kAntiAliasCount-1: off, FXAA, MSAA 2x, 4x, 8x
    int shadows = 3;          // 0..kShadowLevelCount-1: off, very low, low, medium, max, ultra max
    bool filmGrain = true;
    int motionBlur = 0;       // index into kMotionBlurScales: how much turning the view smears it
    bool cameraEffect = true; // lens: colour fringing towards the edges of the picture
    bool fullscreen = false;
    bool vsync = true;
    int fpsCap = 0;           // index into kFpsCaps
    int masterVolume = 8;     // 0..10
    int ambienceVolume = 7;   // 0..10
    int effectsVolume = 8;    // 0..10
    Lang language = Lang::English;
};

// Every row of the settings pages. A row's value is also its id: the `index` of the selected row
// and the hotspot / click ids are these.
enum SettingsItem {
    kSetGraphics, kSetAudio,  // the main page's links to the sub-pages
    kSetResolution, kSetFullscreen, kSetVsync, kSetFpsCap, kSetRenderScale, kSetAntiAlias, kSetShadows, kSetFilmGrain,
    kSetMotionBlur, kSetCameraEffect, kSetMaster, kSetAmbience, kSetEffects, kSetLanguage, kSetBack,
    kSettingsItemCount
};

// The settings are split in pages: the main one (links to Graphics and Audio, the language, Back)
// and one per category. Back on a sub-page returns to the main page.
enum class SettingsPage { Main, Graphics, Audio };

// The rows of a page, top to bottom; the last is always kSetBack.
const std::vector<int>& settingsPageItems(SettingsPage page);

// One frame of the settings pages: keys and clicks move `index` (a SettingsItem of `page`) and
// change values (resolutions stop at `maxResolution`, the largest the display fits). Opening or
// leaving a sub-page changes `page` and `index`. Returns true when the settings should close
// (Back on the main page, or Esc there). `activate` is the game's confirm / interact key.
bool updateSettingsPage(GameSettings& s, SettingsPage& page, int& index, const UiInput& in, bool activate,
                        int maxResolution);

// Persistence: settings as key=value text pairs, and the pairs back into settings (unknown keys
// and bad values are ignored; numbers are clamped).
std::map<std::string, std::string> toKeyValues(const GameSettings& s);
void applyKeyValues(GameSettings& s, const std::map<std::string, std::string>& values);

}  // namespace mythos
