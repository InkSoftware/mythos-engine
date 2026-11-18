#include "engine/platform/apply_settings.h"

#include <algorithm>

namespace mythos {
namespace {

constexpr float kVolumeSteps = 10.0f;  // volumes are stored as 0..10
constexpr double kPercent = 100.0;

}  // namespace

GameSettings loadSettings(const UserFiles& files, const std::string& file) {
    GameSettings settings;
    applyKeyValues(settings, readKeyValueFile(files.path(file)));
    return settings;
}

void saveSettings(const UserFiles& files, const GameSettings& s, const std::string& file) {
    writeKeyValueFile(files.path(file), toKeyValues(s));
}

void applySettings(const GameSettings& s, Window& window, SceneRenderer& scene, AudioEngine& audio) {
    const Resolution res = kResolutions[std::clamp(s.resolution, 0, kResolutionCount - 1)];
    window.applyMode(s.fullscreen, res.w, res.h);
    window.setVsync(s.vsync);
    scene.setRenderScale(s.renderScale);
    audio.setVolumes(s.masterVolume / kVolumeSteps, s.ambienceVolume / kVolumeSteps, s.effectsVolume / kVolumeSteps);
}

void applySettings(const GameSettings& s, CameraState& cam) {
    cam.grain = s.filmGrain;
    cam.shadowLevel = s.shadows;
    cam.materials = std::clamp(s.materials, 0, kMaterialLevelCount - 1);
    cam.antiAlias = s.antiAlias;
    cam.motionBlur = kMotionBlurScales[std::clamp(s.motionBlur, 0, kMotionBlurCount - 1)] / kPercent;
    cam.lensEffect = s.cameraEffect;
}

int largestFittingResolution(const Window& window) {
    int dw = 0, dh = 0;
    window.usableDisplaySize(dw, dh);
    if (dw <= 0 || dh <= 0) return kResolutionCount - 1;  // unknown display: no limit
    int largest = 0;
    for (int i = 0; i < kResolutionCount; ++i)
        if (kResolutions[i].w <= dw && kResolutions[i].h <= dh) largest = i;
    return largest;
}

}  // namespace mythos
