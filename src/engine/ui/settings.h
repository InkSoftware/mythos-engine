#ifndef MYTHOS_ENGINE_UI_SETTINGS_H_
#define MYTHOS_ENGINE_UI_SETTINGS_H_

#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "engine/data/i18n.h"
#include "engine/ui/menu.h"

namespace mythos {
// Window sizes offered by the settings, in pixels. `GameSettings::resolution`
// indexes this table.
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

// Index of the exact size in `kResolutions`, or 0 when it is not listed.
int ResolutionIndex(int w, int h);

// Frame rate limits; 0 means unlimited.
inline constexpr int kFpsCaps[] = {0, 30, 60, 90, 120, 144, 240};
inline constexpr int kFpsCapCount = static_cast<int>(std::size(kFpsCaps));

// Internal render heights in lines for each render scale level.
inline constexpr int kRenderScaleLines[] = {300, 600, 900, 1080, 1440, 2160};
inline constexpr int kRenderScaleCount =
    static_cast<int>(std::size(kRenderScaleLines));

// Number of shadow quality levels, of material quality levels and the motion
// blur strengths in percent.
inline constexpr int kShadowLevelCount = 6;

inline constexpr int kMaterialLevelCount = 3;

inline constexpr int kMotionBlurScales[] = {0, 25, 50, 75, 100, 150, 200};
inline constexpr int kMotionBlurCount =
    static_cast<int>(std::size(kMotionBlurScales));

// Anti-aliasing modes: off, FXAA, then MSAA with the sample counts below (0
// samples for the first two).
inline constexpr int kAntiAliasCount = 5;

inline constexpr int kAntiAliasSamples[kAntiAliasCount] = {0, 0, 2, 4, 8};

// The player's settings. Integer choices are indices into the tables above (or
// a 0..10 volume), kept in range by `ApplyKeyValues` and the UI.
struct GameSettings {
  int resolution = 0;
  int render_scale = 1;
  int anti_alias = 1;
  int shadows = 3;
  int materials = 2;

  bool film_grain = true;
  int motion_blur = 0;
  bool visual_fx = true;
  // Screen-space ambient occlusion (`CameraState::ssao`): off by default.
  bool ambient_occlusion = false;

  // The camcorder tape look (`CameraState::tape`), separate from `visual_fx`.
  bool camera_effect = false;
  bool fullscreen = false;
  bool vsync = true;
  int fps_cap = 0;
  int master_volume = 8;
  int ambience_volume = 7;
  int effects_volume = 8;
  Lang language = Lang::kEnglish;

  // The player's key bindings as one line of JSON
  // (`InputMap::ToJson().dump()`), saved as `bindings` in settings.txt; empty
  // is the defaults. The game reads it with `InputMap::FromJson` and writes it
  // when the controls page reports a change.
  std::string bindings;

  // Not saved: a game that shows the controls page (ui/controls_ui.h) sets it,
  // and the main page then has a Controls row that opens it.
  bool controls_page = false;
};

// Rows of the settings menu. A page shows a subset, in the order given by
// `SettingsPageItems`.
enum SettingsItem {
  kSetGraphics,
  kSetAudio,
  kSetControls,
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
  kSetAmbientOcclusion,
  kSetCameraEffect,
  kSetMaster,
  kSetAmbience,
  kSetEffects,
  kSetLanguage,
  kSetBack,
  kSettingsItemCount
};

// `kControls` is the controls page: the settings menu only switches to it (from
// the Controls row, when `GameSettings::controls_page` is set); the game draws
// and updates it (`UpdateControlsPage`, `DrawControlsPage`) and calls
// `LeaveControlsPage` when it reports back.
enum class SettingsPage { kMain, kGraphics, kAudio, kControls };

// The rows of a page, in display order. The last row of every page is
// `kSetBack`. With `controls` the main page also has `kSetControls`.
const std::vector<int>& SettingsPageItems(SettingsPage page,
                                          bool controls = false);

// Back from the controls page to the main page, on its Controls row.
void LeaveControlsPage(SettingsPage& page, int& index);

// Applies one frame of input to the settings menu: moves the selection (`index`
// is a `SettingsItem`), changes the selected value, switches page, and applies
// a language change at once. `max_resolution` is the highest resolution index
// the display can show. Returns true when the player leaves the menu from the
// main page.
bool UpdateSettingsPage(GameSettings& s, SettingsPage& page, int& index,
                        const UiInput& in, bool activate, int max_resolution);

// Converts settings to the text pairs stored in settings.txt, and back.
// `ApplyKeyValues` ignores unknown keys, clamps numbers into range and leaves
// settings not named untouched; a file from before `visualFx` existed has its
// `cameraEffect` carried over to it.
std::map<std::string, std::string> ToKeyValues(const GameSettings& s);

void ApplyKeyValues(GameSettings& s,
                    const std::map<std::string, std::string>& values);
}

#endif
