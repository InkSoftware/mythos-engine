#include "engine/platform/apply_settings.h"

#include <algorithm>

namespace mythos {
namespace {

constexpr float kVolumeSteps = 10.0f;  // volumes are stored as 0..10
constexpr double kPercent = 100.0;

}  // namespace

GameSettings LoadSettings(const UserFiles& files, const std::string& file) {
  GameSettings settings;
  ApplyKeyValues(settings, ReadKeyValueFile(files.Path(file)));
  return settings;
}

void SaveSettings(const UserFiles& files, const GameSettings& s,
                  const std::string& file) {
  WriteKeyValueFile(files.Path(file), ToKeyValues(s));
}

void ApplySettings(const GameSettings& s, Window& window, SceneRenderer& scene,
                   AudioEngine& audio) {
  const Resolution res =
      kResolutions[std::clamp(s.resolution, 0, kResolutionCount - 1)];
  window.ApplyMode(s.fullscreen, res.w, res.h);
  window.SetVsync(s.vsync);
  scene.SetRenderScale(s.render_scale);
  audio.SetVolumes(s.master_volume / kVolumeSteps,
                   s.ambience_volume / kVolumeSteps,
                   s.effects_volume / kVolumeSteps);
}

void ApplySettings(const GameSettings& s, CameraState& cam) {
  cam.grain = s.film_grain;
  cam.shadow_level = s.shadows;
  cam.materials = std::clamp(s.materials, 0, kMaterialLevelCount - 1);
  cam.anti_alias = s.anti_alias;
  cam.motion_blur =
      kMotionBlurScales[std::clamp(s.motion_blur, 0, kMotionBlurCount - 1)] /
      kPercent;
  cam.lens_effect = s.camera_effect;
  cam.bloom = s.camera_effect ? 0.55 : 0.0;  // the cinematic look: glow too
}

int LargestFittingResolution(const Window& window) {
  int dw = 0, dh = 0;
  window.UsableDisplaySize(dw, dh);
  if (dw <= 0 || dh <= 0) {
    return kResolutionCount - 1;  // unknown display: no limit
  }
  int largest = 0;
  for (int i = 0; i < kResolutionCount; ++i) {
    if (kResolutions[i].w <= dw && kResolutions[i].h <= dh) largest = i;
  }
  return largest;
}

}  // namespace mythos
