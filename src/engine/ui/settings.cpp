#include "engine/ui/settings.h"

#include <algorithm>
#include <cstdlib>

namespace mythos {

int resolutionIndex(int w, int h) {
    for (int i = 0; i < kResolutionCount; ++i)
        if (kResolutions[i].w == w && kResolutions[i].h == h) return i;
    return 0;
}

const std::vector<int>& settingsPageItems(SettingsPage page) {
    static const std::vector<int> main = {kSetGraphics, kSetAudio, kSetLanguage, kSetBack};
    static const std::vector<int> graphics = {kSetResolution, kSetFullscreen, kSetVsync,       kSetFpsCap,
                                              kSetRenderScale, kSetAntiAlias, kSetShadows, kSetMaterials, kSetFilmGrain, kSetMotionBlur,
                                              kSetCameraEffect, kSetBack};
    static const std::vector<int> audio = {kSetMaster, kSetAmbience, kSetEffects, kSetBack};
    return page == SettingsPage::Graphics ? graphics : (page == SettingsPage::Audio ? audio : main);
}

namespace {

constexpr int kMaxVolume = 10;  // volumes are 0..10 (also the number of pips drawn)

// Where `item` sits among a page's rows, or -1.
int rowPosition(const std::vector<int>& items, int item) {
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i] == item) return static_cast<int>(i);
    return -1;
}

// What a frame of input asks of the selected row.
struct RowInput {
    bool click = false;    // a click landed on this row
    int dir = 0;           // -1 / 0 / +1: left / right keys, or the click's step
    bool toggle = false;   // flip an on/off row
    bool choose = false;   // take a link or Back (rather than step a value)
};

// Steps v by dir within [lo, hi]: keys stop at the ends, clicks cycle round.
int stepValue(const RowInput& in, int v, int lo, int hi) {
    if (in.click) {
        const int span = hi - lo + 1;
        return lo + ((v - lo + in.dir) % span + span) % span;
    }
    return std::max(lo, std::min(hi, v + in.dir));
}

// A click on a volume row's pip sets that level (clicking the lit last pip again turns it down).
void clickVolumePip(GameSettings& s, int item, int pip) {
    int* volumes[3] = {&s.masterVolume, &s.ambienceVolume, &s.effectsVolume};
    int& volume = *volumes[item - kSetMaster];
    volume = std::clamp(pip == volume ? pip - 1 : pip, 0, kMaxVolume);
}

// Applies the input to the selected row `index`. Returns true when the row asks to leave its page.
bool applyToRow(GameSettings& s, SettingsPage& page, int& index, const RowInput& in, int maxResolution) {
    switch (index) {
        case kSetGraphics:
            if (in.choose) {
                page = SettingsPage::Graphics;
                index = kSetResolution;
            }
            break;
        case kSetAudio:
            if (in.choose) {
                page = SettingsPage::Audio;
                index = kSetMaster;
            }
            break;
        case kSetResolution: s.resolution = stepValue(in, s.resolution, 0, maxResolution); break;
        case kSetFullscreen:
            if (in.toggle) s.fullscreen = !s.fullscreen;
            break;
        case kSetVsync:
            if (in.toggle) s.vsync = !s.vsync;
            break;
        case kSetFpsCap: s.fpsCap = stepValue(in, s.fpsCap, 0, kFpsCapCount - 1); break;
        case kSetRenderScale: s.renderScale = stepValue(in, s.renderScale, 0, kRenderScaleCount - 1); break;
        case kSetAntiAlias: s.antiAlias = stepValue(in, s.antiAlias, 0, kAntiAliasCount - 1); break;
        case kSetShadows: s.shadows = stepValue(in, s.shadows, 0, kShadowLevelCount - 1); break;
        case kSetMaterials: s.materials = stepValue(in, s.materials, 0, kMaterialLevelCount - 1); break;
        case kSetFilmGrain:
            if (in.toggle) s.filmGrain = !s.filmGrain;
            break;
        case kSetMotionBlur: s.motionBlur = stepValue(in, s.motionBlur, 0, kMotionBlurCount - 1); break;
        case kSetCameraEffect:
            if (in.toggle) s.cameraEffect = !s.cameraEffect;
            break;
        case kSetMaster: s.masterVolume = stepValue(in, s.masterVolume, 0, kMaxVolume); break;
        case kSetAmbience: s.ambienceVolume = stepValue(in, s.ambienceVolume, 0, kMaxVolume); break;
        case kSetEffects: s.effectsVolume = stepValue(in, s.effectsVolume, 0, kMaxVolume); break;
        case kSetLanguage:
            if (in.toggle) {
                s.language = s.language == Lang::English ? Lang::Greek : Lang::English;
                setLanguage(s.language);
            }
            break;
        case kSetBack: return in.choose;
        default: break;
    }
    return false;
}

}  // namespace

bool updateSettingsPage(GameSettings& s, SettingsPage& page, int& index, const UiInput& in, bool activate,
                        int maxResolution) {
    const std::vector<int>& items = settingsPageItems(page);
    const int rowCount = static_cast<int>(items.size());
    int selected = rowPosition(items, index);
    if (selected < 0) selected = 0;  // not a row of this page: start at the top
    if (in.downPressed) selected = (selected + 1) % rowCount;
    if (in.upPressed) selected = (selected + rowCount - 1) % rowCount;
    if (rowPosition(items, in.hoverItem) >= 0) selected = rowPosition(items, in.hoverItem);
    RowInput row;
    row.click = rowPosition(items, in.clickItem) >= 0;
    if (row.click) {
        selected = rowPosition(items, in.clickItem);
        index = items[static_cast<size_t>(selected)];
        if (index >= kSetMaster && index <= kSetEffects && in.clickValue >= 0) {
            clickVolumePip(s, index, in.clickValue);
            return false;
        }
    }
    index = items[static_cast<size_t>(selected)];
    row.dir = row.click ? in.clickStep : (in.rightPressed ? 1 : 0) - (in.leftPressed ? 1 : 0);
    row.toggle = row.dir != 0 || in.confirmPressed;
    row.choose = in.confirmPressed || activate || row.click;  // a link or Back is taken, not stepped
    const bool leave = applyToRow(s, page, index, row, maxResolution);
    // Leaving: a sub-page goes back to the main page (on the link it was opened from); the
    // main page closes the settings.
    if (leave || in.pausePressed) {
        if (page == SettingsPage::Main) return true;
        index = page == SettingsPage::Graphics ? kSetGraphics : kSetAudio;
        page = SettingsPage::Main;
    }
    return false;
}

std::map<std::string, std::string> toKeyValues(const GameSettings& s) {
    const Resolution r = kResolutions[std::clamp(s.resolution, 0, kResolutionCount - 1)];
    return {{"resolution", std::to_string(r.w) + "x" + std::to_string(r.h)},
            {"renderScale", std::to_string(s.renderScale)},
            {"antiAliasing", std::to_string(s.antiAlias)},
            {"shadowMaps", std::to_string(s.shadows)},
            {"materials", std::to_string(s.materials)},
            {"filmGrain", s.filmGrain ? "1" : "0"},
            {"motionBlurScale", std::to_string(kMotionBlurScales[std::clamp(s.motionBlur, 0, kMotionBlurCount - 1)])},
            {"cameraEffect", s.cameraEffect ? "1" : "0"},
            {"fullscreen", s.fullscreen ? "1" : "0"},
            {"vsync", s.vsync ? "1" : "0"},
            {"fpsCap", std::to_string(kFpsCaps[std::clamp(s.fpsCap, 0, kFpsCapCount - 1)])},
            {"masterVolume", std::to_string(s.masterVolume)},
            {"ambienceVolume", std::to_string(s.ambienceVolume)},
            {"effectsVolume", std::to_string(s.effectsVolume)},
            {"language", s.language == Lang::Greek ? "el" : "en"}};
}

void applyKeyValues(GameSettings& s, const std::map<std::string, std::string>& values) {
    auto num = [](const std::string& text, int lo, int hi) { return std::clamp(std::atoi(text.c_str()), lo, hi); };
    for (const auto& [key, val] : values) {
        if (key == "resolution") {
            const size_t sep = val.find('x');  // "1280x720"
            if (sep != std::string::npos && sep > 0 && sep + 1 < val.size())
                s.resolution = resolutionIndex(std::atoi(val.substr(0, sep).c_str()), std::atoi(val.substr(sep + 1).c_str()));
        } else if (key == "renderScale") s.renderScale = num(val, 0, kRenderScaleCount - 1);
        else if (key == "antiAliasing") s.antiAlias = num(val, 0, kAntiAliasCount - 1);
        else if (key == "shadowMaps") s.shadows = num(val, 0, kShadowLevelCount - 1);
        else if (key == "materials") s.materials = num(val, 0, kMaterialLevelCount - 1);
        else if (key == "filmGrain") s.filmGrain = val != "0";
        else if (key == "motionBlurScale") {
            const int pct = std::atoi(val.c_str());
            s.motionBlur = 0;
            for (int i = 0; i < kMotionBlurCount; ++i)
                if (kMotionBlurScales[i] == pct) s.motionBlur = i;
        }
        else if (key == "cameraEffect") s.cameraEffect = val != "0";
        else if (key == "vsync") s.vsync = val != "0";
        else if (key == "fpsCap") {
            const int fps = std::atoi(val.c_str());
            s.fpsCap = 0;
            for (int i = 0; i < kFpsCapCount; ++i)
                if (kFpsCaps[i] == fps) s.fpsCap = i;
        }
        else if (key == "fullscreen") s.fullscreen = val != "0";
        else if (key == "masterVolume") s.masterVolume = num(val, 0, kMaxVolume);
        else if (key == "ambienceVolume") s.ambienceVolume = num(val, 0, kMaxVolume);
        else if (key == "effectsVolume") s.effectsVolume = num(val, 0, kMaxVolume);
        else if (key == "language") s.language = val == "el" ? Lang::Greek : Lang::English;
    }
}

}  // namespace mythos
