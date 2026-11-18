#include "engine/platform/apply_settings.h"

#include <algorithm>

namespace mythos {

GameSettings loadSettings(const UserFiles& files, const std::string& file) {
    GameSettings s;
    applyKeyValues(s, readKeyValueFile(files.path(file)));
    return s;
}

void saveSettings(const UserFiles& files, const GameSettings& s, const std::string& file) {
    writeKeyValueFile(files.path(file), toKeyValues(s));
}

void applySettings(const GameSettings& s, Window& window, SceneRenderer& scene, AudioEngine& audio) {
    const Resolution res = kResolutions[std::clamp(s.resolution, 0, kResolutionCount - 1)];
    window.applyMode(s.fullscreen, res.w, res.h);
    window.setVsync(s.vsync);
    scene.setRenderScale(s.renderScale);
    audio.setVolumes(s.masterVolume / 10.0f, s.ambienceVolume / 10.0f, s.effectsVolume / 10.0f);
}

void applySettings(const GameSettings& s, CameraState& cam) {
    cam.grain = s.filmGrain;
    cam.shadowLevel = s.shadows;
    cam.materials = std::clamp(s.materials, 0, kMaterialLevelCount - 1);
    cam.antiAlias = s.antiAlias;
    cam.motionBlur = kMotionBlurScales[std::clamp(s.motionBlur, 0, kMotionBlurCount - 1)] / 100.0;
    cam.lensEffect = s.cameraEffect;
}

int largestFittingResolution(const Window& window) {
    int dw = 0, dh = 0;
    window.usableDisplaySize(dw, dh);
    if (dw <= 0 || dh <= 0) return kResolutionCount - 1;  // unknown display: no limit
    int fits = 0;
    for (int i = 0; i < kResolutionCount; ++i)
        if (kResolutions[i].w <= dw && kResolutions[i].h <= dh) fits = i;
    return fits;
}

}  // namespace mythos
