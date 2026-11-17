#pragma once
// The platform side of GameSettings: reading and writing them in the user's preferences, and
// making the window, renderer, mixer and camera follow them. A game's main loop calls these
// instead of repeating them, so a new setting needs no change in the game.
#include <string>

#include "engine/platform/app.h"
#include "engine/platform/audio.h"
#include "engine/platform/gl/scene_renderer.h"
#include "engine/platform/window.h"
#include "engine/ui/settings.h"

namespace mythos {

// The settings saved in `file` of the user's files (defaults for what is missing).
GameSettings loadSettings(const UserFiles& files, const std::string& file = "settings.txt");
void saveSettings(const UserFiles& files, const GameSettings& s, const std::string& file = "settings.txt");

// Window mode and vsync, render scale and volumes. Cheap to call every frame: each only acts on a change.
void applySettings(const GameSettings& s, Window& window, SceneRenderer& scene, AudioEngine& audio);
// The camera's graphics options: film grain, shadows, anti-aliasing, motion blur and lens.
void applySettings(const GameSettings& s, CameraState& cam);

// The largest kResolutions index that fits the window's display (no limit when its size is unknown).
int largestFittingResolution(const Window& window);

}  // namespace mythos
