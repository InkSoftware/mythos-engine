#include "engine/platform/apply_settings.h"

#include <algorithm>

namespace mythos {
namespace {
// Volume settings are integers 0..10.
constexpr float kVolumeSteps = 10.0f;
constexpr double kPercent = 100.0;
}

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
  cam.lens_effect = s.visual_fx;
  cam.bloom = s.visual_fx ? 0.55 : 0.0;
  cam.tape = s.camera_effect;
}

int LargestFittingResolution(const Window& window) {
  int dw = 0, dh = 0;
  window.UsableDisplaySize(dw, dh);
  if (dw <= 0 || dh <= 0) {
    return kResolutionCount - 1;
  }
  int largest = 0;
  for (int i = 0; i < kResolutionCount; ++i) {
    if (kResolutions[i].w <= dw && kResolutions[i].h <= dh) largest = i;
  }
  return largest;
}
}
