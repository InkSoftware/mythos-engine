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
                                              kSetRenderScale, kSetAntiAlias, kSetShadows, kSetFilmGrain, kSetMotionBlur,
                                              kSetCameraEffect, kSetBack};
    static const std::vector<int> audio = {kSetMaster, kSetAmbience, kSetEffects, kSetBack};
    return page == SettingsPage::Graphics ? graphics : (page == SettingsPage::Audio ? audio : main);
}

bool updateSettingsPage(GameSettings& s, SettingsPage& page, int& index, const UiInput& in, bool activate,
                        int maxResolution) {
    const std::vector<int>& items = settingsPageItems(page);
    const int n = static_cast<int>(items.size());
    auto position = [&](int item) {
        for (int i = 0; i < n; ++i)
            if (items[static_cast<size_t>(i)] == item) return i;
        return -1;
    };
    int at = position(index);
    if (at < 0) at = 0;  // not a row of this page: start at the top
    if (in.downPressed) at = (at + 1) % n;
    if (in.upPressed) at = (at + n - 1) % n;
    if (position(in.hoverItem) >= 0) at = position(in.hoverItem);
    const bool click = position(in.clickItem) >= 0;
    if (click) {
        at = position(in.clickItem);
        index = items[static_cast<size_t>(at)];
        // Clicking a volume pip sets that level (clicking the lit last pip again turns it down).
        int* volumes[3] = {&s.masterVolume, &s.ambienceVolume, &s.effectsVolume};
        if (index >= kSetMaster && index <= kSetEffects && in.clickValue >= 0) {
            int& v = *volumes[index - kSetMaster];
            v = std::clamp(in.clickValue == v ? in.clickValue - 1 : in.clickValue, 0, 10);
            return false;
        }
    }
    index = items[static_cast<size_t>(at)];
    // Keys step and stop at the ends; clicks cycle round.
    const int dir = click ? in.clickStep : (in.rightPressed ? 1 : 0) - (in.leftPressed ? 1 : 0);
    auto step = [&](int v, int lo, int hi) {
        if (click) return lo + ((v - lo + dir) % (hi - lo + 1) + (hi - lo + 1)) % (hi - lo + 1);
        return std::max(lo, std::min(hi, v + dir));
    };
    const bool toggle = dir != 0 || in.confirmPressed;
    const bool choose = in.confirmPressed || activate || click;  // a link or Back is taken, not stepped
    bool leave = false;
    switch (index) {
        case kSetGraphics:
            if (choose) {
                page = SettingsPage::Graphics;
                index = kSetResolution;
            }
            break;
        case kSetAudio:
            if (choose) {
                page = SettingsPage::Audio;
                index = kSetMaster;
            }
            break;
        case kSetResolution: s.resolution = step(s.resolution, 0, maxResolution); break;
        case kSetFullscreen:
            if (toggle) s.fullscreen = !s.fullscreen;
            break;
        case kSetVsync:
            if (toggle) s.vsync = !s.vsync;
            break;
        case kSetFpsCap: s.fpsCap = step(s.fpsCap, 0, kFpsCapCount - 1); break;
        case kSetRenderScale: s.renderScale = step(s.renderScale, 0, kRenderScaleCount - 1); break;
        case kSetAntiAlias: s.antiAlias = step(s.antiAlias, 0, kAntiAliasCount - 1); break;
        case kSetShadows: s.shadows = step(s.shadows, 0, kShadowLevelCount - 1); break;
        case kSetFilmGrain:
            if (toggle) s.filmGrain = !s.filmGrain;
            break;
        case kSetMotionBlur: s.motionBlur = step(s.motionBlur, 0, kMotionBlurCount - 1); break;
        case kSetCameraEffect:
            if (toggle) s.cameraEffect = !s.cameraEffect;
            break;
        case kSetMaster: s.masterVolume = step(s.masterVolume, 0, 10); break;
        case kSetAmbience: s.ambienceVolume = step(s.ambienceVolume, 0, 10); break;
        case kSetEffects: s.effectsVolume = step(s.effectsVolume, 0, 10); break;
        case kSetLanguage:
            if (toggle) {
                s.language = s.language == Lang::English ? Lang::Greek : Lang::English;
                setLanguage(s.language);
            }
            break;
        case kSetBack:
            if (choose) leave = true;
            break;
        default: break;
    }
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
    auto num = [](const std::string& v, int lo, int hi) { return std::clamp(std::atoi(v.c_str()), lo, hi); };
    for (const auto& [key, val] : values) {
        if (key == "resolution") {
            const size_t x = val.find('x');  // "1280x720"
            if (x != std::string::npos && x > 0 && x + 1 < val.size())
                s.resolution = resolutionIndex(std::atoi(val.substr(0, x).c_str()), std::atoi(val.substr(x + 1).c_str()));
        } else if (key == "renderScale") s.renderScale = num(val, 0, kRenderScaleCount - 1);
        else if (key == "antiAliasing") s.antiAlias = num(val, 0, kAntiAliasCount - 1);
        else if (key == "shadowMaps") s.shadows = num(val, 0, kShadowLevelCount - 1);
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
        else if (key == "masterVolume") s.masterVolume = num(val, 0, 10);
        else if (key == "ambienceVolume") s.ambienceVolume = num(val, 0, 10);
        else if (key == "effectsVolume") s.effectsVolume = num(val, 0, 10);
        else if (key == "language") s.language = val == "el" ? Lang::Greek : Lang::English;
    }
}

}  // namespace mythos
