// The template game's front end: a window, input, the renderer and a loop. All
// the rules are in game.h (headless); this file only turns them into frames.
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "engine/core/json.h"
#include "engine/data/i18n.h"
#include "engine/data/translation.h"
#include "engine/editor/adapter.h"
#include "engine/gfx/font.h"
#include "engine/gfx/materials.h"
#include "engine/platform/app.h"
#include "engine/platform/apply_settings.h"
#include "engine/platform/gl/scene_renderer.h"
#include "engine/platform/input.h"
#include "engine/platform/window.h"
#include "engine/ui/settings.h"
#include "game.h"

namespace {
using template_game::Game;

// Where the assets are during development: the folders this project was
// configured with. A shipped game packs them (mythos_pack) or copies them
// beside the executable and finds them with mythos::FindAssetDir.
std::string EngineAssets() { return TEMPLATE_ENGINE_ASSETS; }
std::string GameAssets() { return TEMPLATE_GAME_ASSETS; }

bool LoadStrings(mythos::Translation& en, mythos::Translation& el) {
  const auto load = [](const std::string& file, mythos::Translation& out) {
    mythos::json::Value v;
    std::string error;
    if (!mythos::json::ParseFile(GameAssets() + "/data/" + file, v, &error) ||
        !mythos::LoadTranslation(v, file, out, &error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return false;
    }
    return true;
  };
  return load("en.json", en) && load("el.json", el);
}

// A flat texture of a glTF material's colour (linear in the file, sRGB here).
mythos::Surface FlatColor(const std::array<float, 4>& linear) {
  const auto to8 = [](const float c) {
    const float s =
        c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1 / 2.4f) - 0.055f;
    return static_cast<int>(std::lround(std::clamp(s, 0.0f, 1.0f) * 255));
  };
  return {8, 8, mythos::Rgba(to8(linear[0]), to8(linear[1]), to8(linear[2]))};
}

// Column-major matrix: a turn about +y so the model's front (+z) faces the
// heading `angle` (the player's convention, direction (cos, sin) on x / z),
// then a move to (x, z).
void PlaceMatrix(const double x, const double z, const double angle, float* m) {
  const double theta = mythos::kPi / 2 - angle;
  const auto c = static_cast<float>(std::cos(theta));
  const auto s = static_cast<float>(std::sin(theta));
  const float out[16] = {c,
                         0,
                         -s,
                         0,
                         0,
                         1,
                         0,
                         0,
                         s,
                         0,
                         c,
                         0,
                         static_cast<float>(x),
                         0,
                         static_cast<float>(z),
                         1};
  std::copy(out, out + 16, m);
}
}

int main(const int argc, char** const argv) {
  // `--screenshot file.ppm` renders 90 frames, saves the last and exits: a
  // smoke test that needs no one at the keyboard.
  std::string screenshot;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--screenshot") screenshot = argv[i + 1];
  }

  mythos::Translation en, el;
  if (!LoadStrings(en, el)) return 1;
  mythos::InstallTranslations(en, el);

  Game game;
  std::string error;
  if (!game.Load(EngineAssets(), GameAssets(), &error)) {
    std::fprintf(stderr, "cannot load the game: %s\n", error.c_str());
    return 1;
  }

  mythos::Window window;
  if (!window.Open(mythos::Tr("template.title"), 1280, 720)) return 1;
  SDL_SetWindowRelativeMouseMode(window.Sdl(), true);

  // Textures: the engine's common ones, the game's overrides, the models' own,
  // and a flat colour for the guide's material.
  mythos::TextureLibrary textures;
  mythos::AddCommonMaterials(textures);
  textures.LoadOverrides(GameAssets());
  mythos::editor::RegisterModelTextures(textures, EngineAssets() + "/modeltex");
  const mythos::GltfModel& guide = game.guide_model();
  for (size_t i = 0; i < guide.batches.size(); ++i) {
    textures.Add(guide.batches[i].texture, FlatColor(guide.base_colors[i]));
  }

  mythos::SceneRenderer renderer;
  if (!renderer.Init(textures)) {
    std::fprintf(stderr, "the renderer could not start\n");
    return 1;
  }
  renderer.UploadWorld(mythos::BuildSceneGeometry(game.level(), game.models()));
  renderer.UploadDoorPanel(mythos::BuildDoorPanel());
  const int guide_model = renderer.UploadModel(guide.batches);

  const mythos::UserFiles files("mythos", "template-game");
  const std::string save_dir = files.Path("saves");

  const mythos::GameSettings settings;
  mythos::FrameClock clock;
  mythos::Surface overlay(640, 360, 0);
  bool f5_was = false, f9_was = false;

  for (int frame = 0;; ++frame) {
    mythos::MoveInput move;
    mythos::UiInput ui;
    mythos::ActionInput act;
    const mythos::InputPoll poll =
        mythos::PollInput(move, ui, act, true, nullptr);
    if (poll.quit || ui.pause_pressed) break;
    if (poll.toggle_fullscreen) window.ApplyMode(true, 1280, 720);

    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys[SDL_SCANCODE_F5] && !f5_was) game.Save(save_dir, 0);
    if (keys[SDL_SCANCODE_F9] && !f9_was) game.LoadSave(save_dir, 0);
    f5_was = keys[SDL_SCANCODE_F5];
    f9_was = keys[SDL_SCANCODE_F9];

    const double dt = std::min(clock.Step(false), 0.1);
    game.Update(dt, move, act);

    // The camera is the player's eye; lamps and doors come from the game.
    mythos::CameraState cam;
    mythos::ApplySettings(settings, cam);
    cam.x = game.player().pos.x;
    cam.z = game.player().pos.y;
    cam.yaw = game.player().angle;
    cam.pitch = game.player().pitch;
    cam.time = clock.Seconds();
    for (const mythos::Vec3& p : game.LampPositions()) {
      mythos::PointLight lamp;
      lamp.x = p.x;
      lamp.y = p.y;
      lamp.z = p.z;
      lamp.r = 1.0;
      lamp.g = 0.78;
      lamp.b = 0.5;
      lamp.radius = 7.0;
      cam.lights.push_back(lamp);
    }
    std::vector<mythos::DoorState> door_states;
    game.doors().AppendStates(door_states);
    for (const mythos::DoorState& d : door_states) {
      cam.doors.push_back(mythos::MakeDoorInstance(d));
    }
    mythos::ModelInstance pete;
    pete.model = guide_model;
    pete.views = mythos::ModelInstance::kMain | mythos::ModelInstance::kMirror |
                 mythos::ModelInstance::kShadow;
    PlaceMatrix(game.guide_pos().x, game.guide_pos().y, game.guide_angle(),
                pete.matrix);
    pete.skin = mythos::SkinMatrices(guide.skeleton, game.guide_pose());
    cam.instances.push_back(pete);

    // A little text on the overlay.
    std::ranges::fill(overlay.px, 0u);
    mythos::DrawText(overlay, 8, 8, mythos::Tr("template.help"),
                     mythos::Rgba(255, 255, 255));
    if (!game.message().empty()) {
      const std::string line = mythos::Tr(game.message());
      mythos::DrawText(overlay, (overlay.w - mythos::TextWidth(line, 2)) / 2,
                       overlay.h - 40, line, mythos::Rgba(255, 230, 160), 2);
    }

    int w = 0, h = 0;
    window.DrawableSize(w, h);
    renderer.Render(cam, w, h, &overlay);
    if (!screenshot.empty() && frame == 90) {
      mythos::WriteFramebufferPpm(screenshot, w, h);
      break;
    }
    window.Swap();
  }
  window.Close();
  return 0;
}
