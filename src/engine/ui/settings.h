#ifndef MYTHOS_ENGINE_UI_SETTINGS_H_
#define MYTHOS_ENGINE_UI_SETTINGS_H_
// Player options and the settings page that edits them: window size,
// fullscreen, vsync, frame rate limit, render scale, anti-aliasing, shadows,
// materials, film grain, motion blur, camera effect, three volumes and the
// language. The platform layer applies them and keeps them in a key=value file
// (toKeyValues / applyKeyValues).
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "engine/data/i18n.h"
#include "engine/ui/menu.h"

namespace mythos {

// Window sizes offered on the settings page (windowed mode; fullscreen uses the
// desktop).
struct Resolution {
  int w, h;
};
inline constexpr Resolution kResolutions[] = {
    {.w = 960, .h = 600},   {.w = 1280, .h = 720},  {.w = 1280, .h = 800},
    {.w = 1366, .h = 768},  {.w = 1600, .h = 900},  {.w = 1680, .h = 1050},
    {.w = 1920, .h = 1080}, {.w = 1920, .h = 1200}, {.w = 2560, .h = 1440},
    {.w = 3840, .h = 2160}};
inline constexpr int kResolutionCount =
    static_cast<int>(std::size(kResolutions));

// The index of w x h in kResolutions, or of the default (960x600) if it is not
// listed.
int ResolutionIndex(int w, int h);

// Frame rate limits offered on the settings page (0 = none: vsync, if on, still
// paces the game).
inline constexpr int kFpsCaps[] = {0, 30, 60, 90, 120, 144, 240};
inline constexpr int kFpsCapCount = static_cast<int>(std::size(kFpsCaps));

// Render scale presets: the height in lines of the internal picture (its width
// follows the window's aspect). The last ones are for high-end systems.
inline constexpr int kRenderScaleLines[] = {300, 600, 900, 1080, 1440, 2160};
inline constexpr int kRenderScaleCount =
    static_cast<int>(std::size(kRenderScaleLines));

// Shadow map levels: 0 off, then very low, low, medium, max, ultra max (see
// shadowLevel in the renderer: more lamps cast shadows, from bigger maps, at
// each step).
inline constexpr int kShadowLevelCount = 6;

// Material levels: 0 off (no specular, metal or occlusion: the flat look), 1
// basic (specular, metal and baked occlusion), 2 full (adds reflections of the
// room's ambient light).
inline constexpr int kMaterialLevelCount = 3;

// Motion blur strengths in percent (0 = off), as offered on the settings page.
inline constexpr int kMotionBlurScales[] = {0, 25, 50, 75, 100, 150, 200};
inline constexpr int kMotionBlurCount =
    static_cast<int>(std::size(kMotionBlurScales));

// Anti-aliasing modes: off, FXAA (a cheap filter), then multisampling (MSAA) at
// 2x, 4x and 8x.
inline constexpr int kAntiAliasCount = 5;
// The samples of each mode: none for off and FXAA (a filter on the finished
// picture).
inline constexpr int kAntiAliasSamples[kAntiAliasCount] = {0, 0, 2, 4, 8};

struct GameSettings {
  int resolution = 0;    // index into kResolutions
  int render_scale = 1;  // index into kRenderScaleLines
  int anti_alias = 1;    // 0..kAntiAliasCount-1: off, FXAA, MSAA 2x, 4x, 8x
  int shadows =
      3;  // 0..kShadowLevelCount-1: off, very low, low, medium, max, ultra max
  int materials = 2;  // 0..kMaterialLevelCount-1: off, basic, full (see
                      // kMaterialLevelCount)
  bool film_grain = true;
  int motion_blur =
      0;  // index into kMotionBlurScales: how much turning the view smears it
  bool visual_fx = true;  // the cinematic look: lens colour fringing towards
                          // the edges of the picture, and bloom
  bool camera_effect =
      false;  // the camcorder (VHS tape) look, with its REC display
  bool fullscreen = false;
  bool vsync = true;
  int fps_cap = 0;          // index into kFpsCaps
  int master_volume = 8;    // 0..10
  int ambience_volume = 7;  // 0..10
  int effects_volume = 8;   // 0..10
  Lang language = Lang::kEnglish;
};

// Every row of the settings pages. A row's value is also its id: the `index` of
// the selected row and the hotspot / click ids are these.
enum SettingsItem {
  kSetGraphics,
  kSetAudio,  // the main page's links to the sub-pages
  kSetResolution,
  kSetFullscreen,
  kSetVsync,
  kSetFpsCap,
  kSetRenderScale,
  kSetAntiAlias,
  kSetShadows,
  kSetMaterials,
  kSetFilmGrain,
  kSetMotionBlur,
  kSetVisualFx,
  kSetCameraEffect,
  kSetMaster,
  kSetAmbience,
  kSetEffects,
  kSetLanguage,
  kSetBack,
  kSettingsItemCount
};

// The settings are split in pages: the main one (links to Graphics and Audio,
// the language, Back) and one per category. Back on a sub-page returns to the
// main page.
enum class SettingsPage { kMain, kGraphics, kAudio };

// The rows of a page, top to bottom; the last is always kSetBack.
const std::vector<int>& SettingsPageItems(SettingsPage page);

// One frame of the settings pages: keys and clicks move `index` (a SettingsItem
// of `page`) and change values (resolutions stop at `maxResolution`, the
// largest the display fits). Opening or leaving a sub-page changes `page` and
// `index`. Returns true when the settings should close (Back on the main page,
// or Esc there). `activate` is the game's confirm / interact key.
bool UpdateSettingsPage(GameSettings& s, SettingsPage& page, int& index,
                        const UiInput& in, bool activate, int max_resolution);

// Persistence: settings as key=value text pairs, and the pairs back into
// settings (unknown keys and bad values are ignored; numbers are clamped).
std::map<std::string, std::string> ToKeyValues(const GameSettings& s);
void ApplyKeyValues(GameSettings& s,
                    const std::map<std::string, std::string>& values);

}  // namespace mythos

#endif  // MYTHOS_ENGINE_UI_SETTINGS_H_
