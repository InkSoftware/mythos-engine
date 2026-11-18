#include "engine/ui/settings_ui.h"

#include <algorithm>

#include "engine/data/i18n.h"

namespace mythos {
namespace {

// One row: what it is called, what it shows at the right (or a 0..10 meter), whether it opens a page.
struct Row {
    std::string label, value;
    int meter = -1;
    bool link = false;
};

Row rowFor(int item, const GameSettings& s, const std::string& languageName) {
    const std::string scales[kRenderScaleCount] = {tr("settings.low"),      tr("settings.medium"), tr("settings.high"),
                                                   tr("settings.veryHigh"), tr("settings.ultra"),  tr("settings.ultraMax")};
    const std::string shadowNames[kShadowLevelCount] = {tr("common.off"),    tr("settings.veryLow"), tr("settings.low"),
                                                        tr("settings.medium"), tr("settings.max"),   tr("settings.ultraMax")};
    const std::string aaNames[kAntiAliasCount] = {tr("common.off"), "FXAA", "MSAA 2X", "MSAA 4X", "MSAA 8X"};
    const std::string materialNames[kMaterialLevelCount] = {tr("common.off"), tr("settings.basic"), tr("settings.full")};
    const auto onOff = [](bool v) { return tr(v ? "common.on" : "common.off"); };
    const Resolution res = kResolutions[std::clamp(s.resolution, 0, kResolutionCount - 1)];
    const int fps = kFpsCaps[std::clamp(s.fpsCap, 0, kFpsCapCount - 1)];
    const int blur = kMotionBlurScales[std::clamp(s.motionBlur, 0, kMotionBlurCount - 1)];
    switch (item) {
        case kSetGraphics: return {tr("settings.graphics"), "›", -1, true};
        case kSetAudio: return {tr("settings.audio"), "›", -1, true};
        case kSetResolution:
            return {tr("settings.resolution"),
                    s.fullscreen ? tr("settings.desktop") : std::to_string(res.w) + "×" + std::to_string(res.h)};
        case kSetFullscreen: return {tr("settings.fullscreen"), onOff(s.fullscreen)};
        case kSetVsync: return {tr("settings.vsync"), onOff(s.vsync)};
        case kSetFpsCap: return {tr("settings.fpsCap"), fps == 0 ? tr("settings.unlimited") : std::to_string(fps)};
        case kSetRenderScale: return {tr("settings.renderScale"), scales[std::clamp(s.renderScale, 0, kRenderScaleCount - 1)]};
        case kSetAntiAlias: return {tr("settings.antiAliasing"), aaNames[std::clamp(s.antiAlias, 0, kAntiAliasCount - 1)]};
        case kSetShadows: return {tr("settings.shadows"), shadowNames[std::clamp(s.shadows, 0, kShadowLevelCount - 1)]};
        case kSetMaterials: return {tr("settings.materials"), materialNames[std::clamp(s.materials, 0, kMaterialLevelCount - 1)]};
        case kSetFilmGrain: return {tr("settings.filmGrain"), onOff(s.filmGrain)};
        case kSetMotionBlur: return {tr("settings.motionBlur"), blur == 0 ? tr("common.off") : std::to_string(blur) + "%"};
        case kSetCameraEffect: return {tr("settings.cameraEffect"), onOff(s.cameraEffect)};
        case kSetMaster: return {tr("settings.masterVolume"), "", s.masterVolume};
        case kSetAmbience: return {tr("settings.ambience"), "", s.ambienceVolume};
        case kSetEffects: return {tr("settings.effects"), "", s.effectsVolume};
        case kSetLanguage: return {tr("settings.language"), languageName};
        default: return {tr("settings.back"), ""};
    }
}

}  // namespace

void drawSettingsPage(UiCanvas& c, const MenuTheme& t, const GameSettings& s, SettingsPage page, int index,
                      const std::string& languageName, double timeSec) {
    Surface& fb = c.fb;
    fillRect(fb, 0, 0, fb.w, fb.h, t.backdrop);
    const int px = (fb.w - t.pageW) / 2, py = (fb.h - t.pageH) / 2;
    const char* titles[3] = {"settings.title", "settings.graphics", "settings.audio"};
    c.text(t.heading, fb.w / 2.0f, py + t.titleY, tr(titles[static_cast<int>(page)]), Align::Center);

    Style rowStyle = t.item;
    rowStyle.size = t.rowSize;
    float y = py + t.firstRowY;
    // A long page squeezes its rows so that Back stays clear of the hint at the bottom.
    const int rows = static_cast<int>(settingsPageItems(page).size());
    const float pitch = std::min(t.rowPitch, (fb.h - t.hintFromBottom - 20.0f - t.backGap - y) / static_cast<float>(std::max(rows - 1, 1)));
    const float left = px + t.margin, right = px + t.pageW - t.margin;
    for (const int i : settingsPageItems(page)) {
        if (i == kSetBack) y += t.backGap;  // Back stands apart
        const Row r = rowFor(i, s, languageName);
        const bool selected = index == i;
        // The resolution only applies to a window; in fullscreen it waits, dimmed.
        const bool idle = i == kSetResolution && s.fullscreen;
        Style st = rowStyle.recolored(selected ? t.selected : (idle ? t.dim : t.text));
        if (selected) {
            st.fx = selectionGlow(timeSec, 3.0f);
            c.text(st, left - 5, y, "›", Align::Right);
        }
        c.text(st, left, y, r.label);
        c.hot(i, left - 12, y - 1, right + 4, y + 10);
        if (r.meter >= 0) {  // a row of pips, lit up to the volume; each one is clickable
            const int bx = static_cast<int>(right) - 10 * 6 + 2, by = static_cast<int>(y + 3.5f);
            for (int k = 0; k < 10; ++k) {
                const uint32_t col = k < r.meter ? (selected ? t.selected : t.pipOn) : t.pipOff;
                fillRect(fb, bx + k * 6, by, bx + k * 6 + 4, by + 4, col);
                c.hot(i, bx + k * 6 - 1.0f, by - 4.0f, bx + k * 6 + 5.0f, by + 8.0f, k + 1);
            }
        } else if (i == kSetResolution && !s.fullscreen) {  // lining figures for the window size
            Style digits = st;
            digits.font = t.number.font;
            digits.size = t.numberSize;
            c.text(digits, right, y + 1, r.value, Align::Right);
        } else if (!r.link) {
            c.text(st, right, y, r.value, Align::Right);
        }
        y += pitch;
    }
    c.text(t.hint, fb.w / 2.0f, fb.h - t.hintFromBottom, tr("settings.hint"), Align::Center);
}

}  // namespace mythos
