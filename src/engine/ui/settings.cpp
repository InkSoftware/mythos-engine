#include "engine/ui/settings.h"

#include <algorithm>
#include <cstdlib>

namespace mythos {

int ResolutionIndex(int w, int h) {
  for (int i = 0; i < kResolutionCount; ++i) {
    if (kResolutions[i].w == w && kResolutions[i].h == h) return i;
  }
  return 0;
}

const std::vector<int>& SettingsPageItems(SettingsPage page) {
  const static std::vector<int> kMain = {kSetGraphics, kSetAudio, kSetLanguage,
                                         kSetBack};
  const static std::vector<int> kGraphics = {
      kSetResolution,  kSetFullscreen, kSetVsync,        kSetFpsCap,
      kSetRenderScale, kSetAntiAlias,  kSetShadows,      kSetMaterials,
      kSetFilmGrain,   kSetMotionBlur, kSetCameraEffect, kSetBack};
  const static std::vector<int> kAudio = {kSetMaster, kSetAmbience, kSetEffects,
                                          kSetBack};
  return page == SettingsPage::kGraphics
             ? kGraphics
             : (page == SettingsPage::kAudio ? kAudio : kMain);
}

namespace {

constexpr int kMaxVolume =
    10;  // volumes are 0..10 (also the number of pips drawn)

// Where `item` sits among a page's rows, or -1.
int RowPosition(const std::vector<int>& items, int item) {
  for (size_t i = 0; i < items.size(); ++i) {
    if (items[i] == item) return static_cast<int>(i);
  }
  return -1;
}

// What a frame of input asks of the selected row.
struct RowInput {
  bool click = false;   // a click landed on this row
  int dir = 0;          // -1 / 0 / +1: left / right keys, or the click's step
  bool toggle = false;  // flip an on/off row
  bool choose = false;  // take a link or Back (rather than step a value)
};

// Steps v by dir within [lo, hi]: keys stop at the ends, clicks cycle round.
int StepValue(const RowInput& in, int v, int lo, int hi) {
  if (in.click) {
    const int span = hi - lo + 1;
    return lo + ((v - lo + in.dir) % span + span) % span;
  }
  return std::max(lo, std::min(hi, v + in.dir));
}

// A click on a volume row's pip sets that level (clicking the lit last pip
// again turns it down).
void ClickVolumePip(GameSettings& s, int item, int pip) {
  int* const volumes[3] = {&s.master_volume, &s.ambience_volume,
                           &s.effects_volume};
  int& volume = *volumes[item - kSetMaster];
  volume = std::clamp(pip == volume ? pip - 1 : pip, 0, kMaxVolume);
}

// Applies the input to the selected row `index`. Returns true when the row asks
// to leave its page.
bool ApplyToRow(GameSettings& s, SettingsPage& page, int& index,
                const RowInput& in, int max_resolution) {
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

}  // namespace

bool UpdateSettingsPage(GameSettings& s, SettingsPage& page, int& index,
                        const UiInput& in, bool activate, int max_resolution) {
  const std::vector<int>& items = SettingsPageItems(page);
  const int row_count = static_cast<int>(items.size());
  int selected = RowPosition(items, index);
  selected = std::max(selected, 0);  // not a row of this page: start at the top
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
  row.choose = in.confirm_pressed || activate ||
               row.click;  // a link or Back is taken, not stepped
  const bool leave = ApplyToRow(s, page, index, row, max_resolution);
  // Leaving: a sub-page goes back to the main page (on the link it was opened
  // from); the main page closes the settings.
  if (leave || in.pause_pressed) {
    if (page == SettingsPage::kMain) return true;
    index = page == SettingsPage::kGraphics ? kSetGraphics : kSetAudio;
    page = SettingsPage::kMain;
  }
  return false;
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
  const auto num = [](const std::string& text, int lo, int hi) {
    return std::clamp(std::atoi(text.c_str()), lo, hi);
  };
  for (const auto& [key, val] : values) {
    if (key == "resolution") {
      const size_t sep = val.find('x');  // "1280x720"
      if (sep != std::string::npos && sep > 0 && sep + 1 < val.size()) {
        s.resolution = ResolutionIndex(std::atoi(val.substr(0, sep).c_str()),
                                       std::atoi(val.substr(sep + 1).c_str()));
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
      const int pct = std::atoi(val.c_str());
      s.motion_blur = 0;
      for (int i = 0; i < kMotionBlurCount; ++i) {
        if (kMotionBlurScales[i] == pct) s.motion_blur = i;
      }
    } else if (key == "cameraEffect") {
      s.camera_effect = val != "0";
    } else if (key == "vsync") {
      s.vsync = val != "0";
    } else if (key == "fpsCap") {
      const int fps = std::atoi(val.c_str());
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
}

}  // namespace mythos
