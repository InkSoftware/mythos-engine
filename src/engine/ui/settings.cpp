#include "engine/ui/settings.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace mythos {
int ResolutionIndex(const int w, const int h) {
  for (int i = 0; i < kResolutionCount; ++i) {
    if (kResolutions[i].w == w && kResolutions[i].h == h) return i;
  }
  return 0;
}

const std::vector<int>& SettingsPageItems(const SettingsPage page,
                                          const bool controls) {
  const static std::vector<int> kMain = {kSetGraphics, kSetAudio, kSetLanguage,
                                         kSetBack};
  const static std::vector<int> kMainWithControls = {
      kSetGraphics, kSetAudio, kSetControls, kSetLanguage, kSetBack};
  const static std::vector<int> kGraphics = {
      kSetResolution,       kSetFullscreen, kSetVsync,    kSetFpsCap,
      kSetRenderScale,      kSetAntiAlias,  kSetShadows,  kSetMaterials,
      kSetFilmGrain,        kSetMotionBlur, kSetVisualFx, kSetCameraEffect,
      kSetAmbientOcclusion, kSetBack};
  const static std::vector<int> kAudio = {kSetMaster, kSetAmbience, kSetEffects,
                                          kSetBack};
  if (page == SettingsPage::kGraphics) return kGraphics;
  if (page == SettingsPage::kAudio) return kAudio;
  return controls && page == SettingsPage::kMain ? kMainWithControls : kMain;
}

namespace {
constexpr int kMaxVolume = 10;

int RowPosition(const std::vector<int>& items, const int item) {
  for (size_t i = 0; i < items.size(); ++i) {
    if (items[i] == item) return static_cast<int>(i);
  }
  return -1;
}

struct RowInput {
  bool click = false;
  int dir = 0;
  bool toggle = false;
  bool choose = false;
};

// Moves a value by `in.dir`: wrapping at the ends for a click, stopping at them
// for the keyboard.
int StepValue(const RowInput& in, const int v, const int lo, const int hi) {
  if (in.click) {
    const int span = hi - lo + 1;
    return lo + ((v - lo + in.dir) % span + span) % span;
  }
  return std::max(lo, std::min(hi, v + in.dir));
}

// Sets a volume from a click on pip 1..10; clicking the current top pip lowers
// it by one, so a volume can be turned to 0.
void ClickVolumePip(GameSettings& s, const int item, const int pip) {
  int* const volumes[3] = {&s.master_volume, &s.ambience_volume,
                           &s.effects_volume};
  int& volume = *volumes[item - kSetMaster];
  volume = std::clamp(pip == volume ? pip - 1 : pip, 0, kMaxVolume);
}

// Applies the input to the selected row. Returns true when the row is Back and
// was chosen.
bool ApplyToRow(GameSettings& s, SettingsPage& page, int& index,
                const RowInput& in, const int max_resolution) {
  switch (index) {
    case kSetGraphics:
      if (in.choose) {
        page = SettingsPage::kGraphics;
        index = kSetResolution;
      }
      break;
    case kSetAudio:
      if (in.choose) {
        page = SettingsPage::kAudio;
        index = kSetMaster;
      }
      break;
    case kSetControls:
      if (in.choose) page = SettingsPage::kControls;
      break;
    case kSetResolution:
      s.resolution = StepValue(in, s.resolution, 0, max_resolution);
      break;
    case kSetFullscreen:
      if (in.toggle) s.fullscreen = !s.fullscreen;
      break;
    case kSetVsync:
      if (in.toggle) s.vsync = !s.vsync;
      break;
    case kSetFpsCap:
      s.fps_cap = StepValue(in, s.fps_cap, 0, kFpsCapCount - 1);
      break;
    case kSetRenderScale:
      s.render_scale = StepValue(in, s.render_scale, 0, kRenderScaleCount - 1);
      break;
    case kSetAntiAlias:
      s.anti_alias = StepValue(in, s.anti_alias, 0, kAntiAliasCount - 1);
      break;
    case kSetShadows:
      s.shadows = StepValue(in, s.shadows, 0, kShadowLevelCount - 1);
      break;
    case kSetMaterials:
      s.materials = StepValue(in, s.materials, 0, kMaterialLevelCount - 1);
      break;
    case kSetFilmGrain:
      if (in.toggle) s.film_grain = !s.film_grain;
      break;
    case kSetMotionBlur:
      s.motion_blur = StepValue(in, s.motion_blur, 0, kMotionBlurCount - 1);
      break;
    case kSetVisualFx:
      if (in.toggle) s.visual_fx = !s.visual_fx;
      break;
    case kSetAmbientOcclusion:
      if (in.toggle) s.ambient_occlusion = !s.ambient_occlusion;
      break;
    case kSetCameraEffect:
      if (in.toggle) s.camera_effect = !s.camera_effect;
      break;
    case kSetMaster:
      s.master_volume = StepValue(in, s.master_volume, 0, kMaxVolume);
      break;
    case kSetAmbience:
      s.ambience_volume = StepValue(in, s.ambience_volume, 0, kMaxVolume);
      break;
    case kSetEffects:
      s.effects_volume = StepValue(in, s.effects_volume, 0, kMaxVolume);
      break;
    case kSetLanguage:
      if (in.toggle) {
        s.language =
            s.language == Lang::kEnglish ? Lang::kGreek : Lang::kEnglish;
        SetLanguage(s.language);
      }
      break;
    case kSetBack:
      return in.choose;
    default:
      break;
  }
  return false;
}
}

bool UpdateSettingsPage(GameSettings& s, SettingsPage& page, int& index,
                        const UiInput& in, const bool activate,
                        const int max_resolution) {
  // The controls page is the game's to run.
  if (page == SettingsPage::kControls) return false;
  const std::vector<int>& items = SettingsPageItems(page, s.controls_page);
  const int row_count = static_cast<int>(items.size());
  int selected = RowPosition(items, index);
  selected = std::max(selected, 0);
  if (in.down_pressed) selected = (selected + 1) % row_count;
  if (in.up_pressed) selected = (selected + row_count - 1) % row_count;
  if (RowPosition(items, in.hover_item) >= 0) {
    selected = RowPosition(items, in.hover_item);
  }
  RowInput row;
  row.click = RowPosition(items, in.click_item) >= 0;
  if (row.click) {
    selected = RowPosition(items, in.click_item);
    index = items[static_cast<size_t>(selected)];
    if (index >= kSetMaster && index <= kSetEffects && in.click_value >= 0) {
      ClickVolumePip(s, index, in.click_value);
      return false;
    }
  }
  index = items[static_cast<size_t>(selected)];
  row.dir = row.click ? in.click_step
                      : (in.right_pressed ? 1 : 0) - (in.left_pressed ? 1 : 0);
  row.toggle = row.dir != 0 || in.confirm_pressed;
  row.choose = in.confirm_pressed || activate || row.click;
  const bool leave = ApplyToRow(s, page, index, row, max_resolution);

  // Back and pause leave a subpage to the main page (selecting its entry), or
  // the menu from the main page.
  if (leave || in.pause_pressed) {
    if (page == SettingsPage::kMain) return true;
    index = page == SettingsPage::kGraphics ? kSetGraphics : kSetAudio;
    page = SettingsPage::kMain;
  }
  return false;
}

void LeaveControlsPage(SettingsPage& page, int& index) {
  page = SettingsPage::kMain;
  index = kSetControls;
}

std::map<std::string, std::string> ToKeyValues(const GameSettings& s) {
  const Resolution r =
      kResolutions[std::clamp(s.resolution, 0, kResolutionCount - 1)];
  return {{"resolution", std::to_string(r.w) + "x" + std::to_string(r.h)},
          {"renderScale", std::to_string(s.render_scale)},
          {"antiAliasing", std::to_string(s.anti_alias)},
          {"shadowMaps", std::to_string(s.shadows)},
          {"materials", std::to_string(s.materials)},
          {"filmGrain", s.film_grain ? "1" : "0"},
          {"motionBlurScale", std::to_string(kMotionBlurScales[std::clamp(
                                  s.motion_blur, 0, kMotionBlurCount - 1)])},
          {"visualFx", s.visual_fx ? "1" : "0"},
          {"ambientOcclusion", s.ambient_occlusion ? "1" : "0"},
          {"bindings", s.bindings},
          {"cameraEffect", s.camera_effect ? "1" : "0"},
          {"fullscreen", s.fullscreen ? "1" : "0"},
          {"vsync", s.vsync ? "1" : "0"},
          {"fpsCap", std::to_string(
                         kFpsCaps[std::clamp(s.fps_cap, 0, kFpsCapCount - 1)])},
          {"masterVolume", std::to_string(s.master_volume)},
          {"ambienceVolume", std::to_string(s.ambience_volume)},
          {"effectsVolume", std::to_string(s.effects_volume)},
          {"language", s.language == Lang::kGreek ? "el" : "en"}};
}

void ApplyKeyValues(GameSettings& s,
                    const std::map<std::string, std::string>& values) {
  const auto to_int = [](const std::string& text) {
    const long long v = std::strtoll(text.c_str(), nullptr, 10);
    return static_cast<int>(std::clamp<long long>(
        v, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
  };
  const auto num = [&to_int](const std::string& text, const int lo,
                             const int hi) {
    return std::clamp(to_int(text), lo, hi);
  };
  for (const auto& [key, val] : values) {
    if (key == "resolution") {
      const size_t sep = val.find('x');
      if (sep != std::string::npos && sep > 0 && sep + 1 < val.size()) {
        s.resolution = ResolutionIndex(to_int(val.substr(0, sep)),
                                       to_int(val.substr(sep + 1)));
      }
    } else if (key == "renderScale") {
      s.render_scale = num(val, 0, kRenderScaleCount - 1);
    } else if (key == "antiAliasing") {
      s.anti_alias = num(val, 0, kAntiAliasCount - 1);
    } else if (key == "shadowMaps") {
      s.shadows = num(val, 0, kShadowLevelCount - 1);
    } else if (key == "materials") {
      s.materials = num(val, 0, kMaterialLevelCount - 1);
    } else if (key == "filmGrain") {
      s.film_grain = val != "0";
    } else if (key == "motionBlurScale") {
      const int pct = to_int(val);
      s.motion_blur = 0;
      for (int i = 0; i < kMotionBlurCount; ++i) {
        if (kMotionBlurScales[i] == pct) s.motion_blur = i;
      }
    } else if (key == "visualFx") {
      s.visual_fx = val != "0";
    } else if (key == "bindings") {
      // One line only (a newline would end the line in the file).
      s.bindings = val.substr(0, val.find_first_of("\r\n"));
    } else if (key == "ambientOcclusion") {
      s.ambient_occlusion = val != "0";
    } else if (key == "cameraEffect") {
      s.camera_effect = val != "0";
    } else if (key == "vsync") {
      s.vsync = val != "0";
    } else if (key == "fpsCap") {
      const int fps = to_int(val);
      s.fps_cap = 0;
      for (int i = 0; i < kFpsCapCount; ++i) {
        if (kFpsCaps[i] == fps) s.fps_cap = i;
      }
    } else if (key == "fullscreen") {
      s.fullscreen = val != "0";
    } else if (key == "masterVolume") {
      s.master_volume = num(val, 0, kMaxVolume);
    } else if (key == "ambienceVolume") {
      s.ambience_volume = num(val, 0, kMaxVolume);
    } else if (key == "effectsVolume") {
      s.effects_volume = num(val, 0, kMaxVolume);
    } else if (key == "language") {
      s.language = val == "el" ? Lang::kGreek : Lang::kEnglish;
    }
  }

  if (!values.contains("visualFx") && values.contains("cameraEffect")) {
    s.visual_fx = s.camera_effect;
    s.camera_effect = false;
  }
}
}
