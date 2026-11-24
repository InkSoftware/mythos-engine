#include "engine/ui/settings_ui.h"

#include <algorithm>

#include "engine/data/i18n.h"

namespace mythos {
namespace {
constexpr int kPipCount = 10;
constexpr int kPipPitch = 6;
constexpr int kPipSize = 4;
constexpr float kHintMargin = 20.0f;

// What a row shows: label and value text, a volume meter (0..10, or -1 for
// none) and whether it opens a subpage.
struct Row {
  std::string label, value;
  int meter = -1;
  bool link = false;
};

Row RowFor(const int item, const GameSettings& s,
           const std::string& language_name) {
  const std::string scales[kRenderScaleCount] = {
      Tr("settings.low"),      Tr("settings.medium"), Tr("settings.high"),
      Tr("settings.veryHigh"), Tr("settings.ultra"),  Tr("settings.ultraMax")};
  const std::string shadow_names[kShadowLevelCount] = {
      Tr("common.off"),      Tr("settings.veryLow"), Tr("settings.low"),
      Tr("settings.medium"), Tr("settings.max"),     Tr("settings.ultraMax")};
  const std::string aa_names[kAntiAliasCount] = {
      Tr("common.off"), "FXAA", "MSAA 2X", "MSAA 4X", "MSAA 8X"};
  const std::string material_names[kMaterialLevelCount] = {
      Tr("common.off"), Tr("settings.basic"), Tr("settings.full")};
  const auto on_off = [](const bool v) {
    return Tr(v ? "common.on" : "common.off");
  };
  const Resolution res =
      kResolutions[std::clamp(s.resolution, 0, kResolutionCount - 1)];
  const int fps = kFpsCaps[std::clamp(s.fps_cap, 0, kFpsCapCount - 1)];
  const int blur =
      kMotionBlurScales[std::clamp(s.motion_blur, 0, kMotionBlurCount - 1)];
  switch (item) {
    case kSetGraphics:
      return {.label = Tr("settings.graphics"),
              .value = "›",
              .meter = -1,
              .link = true};
    case kSetAudio:
      return {.label = Tr("settings.audio"),
              .value = "›",
              .meter = -1,
              .link = true};
    case kSetControls:
      return {.label = Tr("settings.controls"),
              .value = "›",
              .meter = -1,
              .link = true};
    case kSetResolution:
      return {.label = Tr("settings.resolution"),
              .value = s.fullscreen ? Tr("settings.desktop")
                                    : std::to_string(res.w) + "×" +
                                          std::to_string(res.h)};
    case kSetFullscreen:
      return {.label = Tr("settings.fullscreen"),
              .value = on_off(s.fullscreen)};
    case kSetVsync:
      return {.label = Tr("settings.vsync"), .value = on_off(s.vsync)};
    case kSetFpsCap:
      return {
          .label = Tr("settings.fpsCap"),
          .value = fps == 0 ? Tr("settings.unlimited") : std::to_string(fps)};
    case kSetRenderScale:
      return {.label = Tr("settings.renderScale"),
              .value =
                  scales[std::clamp(s.render_scale, 0, kRenderScaleCount - 1)]};
    case kSetAntiAlias:
      return {
          .label = Tr("settings.antiAliasing"),
          .value = aa_names[std::clamp(s.anti_alias, 0, kAntiAliasCount - 1)]};
    case kSetShadows:
      return {
          .label = Tr("settings.shadows"),
          .value =
              shadow_names[std::clamp(s.shadows, 0, kShadowLevelCount - 1)]};
    case kSetMaterials:
      return {.label = Tr("settings.materials"),
              .value = material_names[std::clamp(s.materials, 0,
                                                 kMaterialLevelCount - 1)]};
    case kSetFilmGrain:
      return {.label = Tr("settings.filmGrain"), .value = on_off(s.film_grain)};
    case kSetMotionBlur:
      return {
          .label = Tr("settings.motionBlur"),
          .value = blur == 0 ? Tr("common.off") : std::to_string(blur) + "%"};
    case kSetVisualFx:
      return {.label = Tr("settings.visualFx"), .value = on_off(s.visual_fx)};
    case kSetAmbientOcclusion:
      return {.label = Tr("settings.ambientOcclusion"),
              .value = on_off(s.ambient_occlusion)};
    case kSetCameraEffect:
      return {.label = Tr("settings.cameraEffect"),
              .value = on_off(s.camera_effect)};
    case kSetMaster:
      return {.label = Tr("settings.masterVolume"),
              .value = "",
              .meter = s.master_volume};
    case kSetAmbience:
      return {.label = Tr("settings.ambience"),
              .value = "",
              .meter = s.ambience_volume};
    case kSetEffects:
      return {.label = Tr("settings.effects"),
              .value = "",
              .meter = s.effects_volume};
    case kSetLanguage:
      return {.label = Tr("settings.language"), .value = language_name};
    default:
      return {.label = Tr("settings.back"), .value = ""};
  }
}
}

void DrawSettingsPage(UiCanvas& c, const MenuTheme& t, const GameSettings& s,
                      const SettingsPage page, const int index,
                      const std::string& language_name, const double time_sec) {
  // The controls page is drawn by the game (DrawControlsPage).
  if (page == SettingsPage::kControls) return;
  Surface& fb = c.fb;
  FillRect(fb, 0, 0, fb.w, fb.h, t.backdrop);
  const int px = (fb.w - t.page_w) / 2, py = (fb.h - t.page_h) / 2;
  const auto fpx = static_cast<float>(px), fpy = static_cast<float>(py),
             fw = static_cast<float>(fb.w), fh = static_cast<float>(fb.h);
  const char* titles[3] = {"settings.title", "settings.graphics",
                           "settings.audio"};
  c.Text(t.heading, fw / 2.0f, fpy + t.title_y,
         Tr(titles[static_cast<int>(page)]), Align::kCenter);

  Style row_style = t.item;
  row_style.size = t.row_size;
  float y = fpy + t.first_row_y;
  const std::vector<int>& items = SettingsPageItems(page, s.controls_page);
  const int rows = static_cast<int>(items.size());
  // Squeezes the rows together when the window is too short for the usual
  // pitch.
  const float pitch = std::min(
      t.row_pitch, (fh - t.hint_from_bottom - kHintMargin - t.back_gap - y) /
                       static_cast<float>(std::max(rows - 1, 1)));
  const float left = fpx + t.margin,
              right = fpx + static_cast<float>(t.page_w) - t.margin;
  for (const int i : items) {
    if (i == kSetBack) y += t.back_gap;
    const Row r = RowFor(i, s, language_name);
    const bool selected = index == i;

    // Resolution is grayed out in fullscreen, where the desktop size is used.
    const bool idle = i == kSetResolution && s.fullscreen;
    Style st =
        row_style.Recolored(selected ? t.selected : (idle ? t.dim : t.text));
    if (selected) {
      st.fx = SelectionGlow(time_sec, 3.0f);
      c.Text(st, left - 5, y, "›", Align::kRight);
    }
    c.Text(st, left, y, r.label);
    c.Hot(i, left - 12, y - 1, right + 4, y + 10);
    if (r.meter >= 0) {
      const int pips_left = static_cast<int>(right) - kPipCount * kPipPitch + 2,
                pips_top = static_cast<int>(y + 3.5f);
      for (int pip = 0; pip < kPipCount; ++pip) {
        const int pip_x = pips_left + pip * kPipPitch;
        const uint32_t color =
            pip < r.meter ? (selected ? t.selected : t.pip_on) : t.pip_off;
        FillRect(fb, pip_x, pips_top, pip_x + kPipSize, pips_top + kPipSize,
                 color);
        c.Hot(i, static_cast<float>(pip_x) - 1.0f,
              static_cast<float>(pips_top) - 4.0f,
              static_cast<float>(pip_x) + 5.0f,
              static_cast<float>(pips_top) + 8.0f, pip + 1);
      }
    } else if (i == kSetResolution && !s.fullscreen) {
      Style digits = st;
      digits.font = t.number.font;
      digits.size = t.number_size;
      c.Text(digits, right, y + 1, r.value, Align::kRight);
    } else if (!r.link) {
      c.Text(st, right, y, r.value, Align::kRight);
    }
    y += pitch;
  }
  c.Text(t.hint, fw / 2.0f, fh - t.hint_from_bottom, Tr("settings.hint"),
         Align::kCenter);
}
}
