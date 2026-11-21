#ifndef MYTHOS_ENGINE_PLATFORM_APPLY_SETTINGS_H_
#define MYTHOS_ENGINE_PLATFORM_APPLY_SETTINGS_H_

#include <string>

#include "engine/platform/app.h"
#include "engine/platform/audio.h"
#include "engine/platform/gl/scene_renderer.h"
#include "engine/platform/window.h"
#include "engine/ui/settings.h"

namespace mythos {
// Reads settings from the user's key-value file; missing keys keep defaults.
GameSettings LoadSettings(const UserFiles& files,
                          const std::string& file = "settings.txt");

// Writes all settings to the user's key-value file.
void SaveSettings(const UserFiles& files, const GameSettings& s,
                  const std::string& file = "settings.txt");

// Pushes the settings into the window (mode, vsync), renderer (render scale)
// and mixer (volumes).
void ApplySettings(const GameSettings& s, Window& window, SceneRenderer& scene,
                   AudioEngine& audio);

// Copies the per-frame visual settings (grain, shadows, materials, AA, motion
// blur, lens, bloom, tape) into the camera state.
void ApplySettings(const GameSettings& s, CameraState& cam);

// Index into `kResolutions` of the largest entry that fits the usable display
// area (the last index if the display size is unknown).
int LargestFittingResolution(const Window& window);
}

#endif
