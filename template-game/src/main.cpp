// The template game's front end: a window, input, the renderer and a loop. All
// the rules are in game.h (headless); this file only turns them into frames.
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "engine/audio/audio_math.h"
#include "engine/audio/spatial.h"
#include "engine/core/file_watch.h"
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
#include "engine/ui/controls_ui.h"
#include "engine/ui/input_map.h"
#include "engine/ui/settings.h"
#include "engine/world/sound_path.h"
#include "game.h"

namespace {
using template_game::Game;

// Where the assets are during development: the folders this project was
// configured with. A shipped game packs them (mythos_pack) or copies them
// beside the executable and finds them with mythos::FindAssetDir.
std::string EngineAssets() { return TEMPLATE_ENGINE_ASSETS; }
std::string GameAssets() { return TEMPLATE_GAME_ASSETS; }
std::string EngineShaders() { return TEMPLATE_ENGINE_SHADERS; }

bool HasSuffix(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

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
  // `--controls` opens the controls page at the start (for a screenshot).
  // `--ssao` turns on ambient occlusion (the Ambient occlusion setting).
  // `--outdoor [--hour H]` takes the roof off and runs a day: the sun, sky
  // and light follow a time of day that moves on (ten seconds an hour).
  std::string screenshot;
  bool outdoor = false;
  bool ssao = false;
  bool start_controls = false;
  double hour = 20.0;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--outdoor") outdoor = true;
    if (arg == "--ssao") ssao = true;
    if (arg == "--controls") start_controls = true;
    if (i + 1 < argc && arg == "--screenshot") screenshot = argv[i + 1];
    if (i + 1 < argc && arg == "--hour")
      hour = std::strtod(argv[i + 1], nullptr);
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
  renderer.UploadWorld(
      mythos::BuildSceneGeometry(game.level(), game.models(), outdoor));
  renderer.UploadDoorPanel(mythos::BuildDoorPanel());
  const int guide_model = renderer.UploadModel(guide.batches);

  const mythos::UserFiles files("mythos", "template-game");
  const std::string save_dir = files.Path("saves");

  mythos::GameSettings settings;
  settings.ambient_occlusion = ssao;

  // Sound: the engine's common clips. A hum in the back room is a positional
  // source: panned and attenuated by distance, and muffled by the walls and
  // the closed door between it and the player (occlusion).
  mythos::AudioEngine audio;
  int hum = -1;
  mythos::Vec2 hum_at{14.5, 2.5};
  if (audio.Init()) {
    audio.LoadDir(EngineAssets() + "/sounds",
                  {"step_wood1", "step_wood2", "step_wood3", "door_open",
                   "door_close", "wind"});
    hum = audio.AddSource("wind");
  }

  // Input actions: the engine's default keys, with the player's rebinding laid
  // over them from bindings.json. `rebinding` waits for the next key.
  mythos::InputMap bindings = mythos::DefaultGameplayMap();
  {
    mythos::json::Value saved;
    if (mythos::json::Parse(files.Read("bindings.json"), saved)) {
      bindings.FromJson(saved);
    }
  }
  mythos::ActionTracker actions;
  // The controls page (F3): the engine's rebind page, drawn with the SDF font
  // on a text layer over the overlay. The game pauses while it is open.
  mythos::SdfFont font;
  if (!font.LoadFile(EngineAssets() + "/fonts/Alegreya.ttf", &error)) {
    std::fprintf(stderr, "no font: %s\n", error.c_str());
    return 1;
  }
  renderer.AddFont(font);
  mythos::MenuTheme theme;
  theme.heading = {&font, 22.0f, 0xFFE8C45A, mythos::DropShadow()};
  theme.item = {&font, 13.0f, 0xFFE6DCC8, mythos::DropShadow()};
  theme.hint = {&font, 10.0f, 0xFF8A807A, mythos::DropShadow()};
  theme.number = theme.item;
  theme.page_w = 440;
  theme.page_h = 300;
  theme.first_row_y = 38.0f;
  theme.row_pitch = 22.0f;
  mythos::ControlsState controls;
  mythos::TextLayer text_layer;
  bool controls_open = start_controls, f3_was = false;

  // Hot reload (development): edit the level, a texture or a shader and see it
  // without restarting. The paths are the ones compiled in by CMake.
  mythos::FileWatcher watcher;
  watcher.Add(GameAssets());
  watcher.Add(EngineShaders());
  // The opening cutscene (`--no-intro` skips it).
  bool intro = true;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--no-intro") intro = false;
  }
  if (intro) {
    std::string why;
    if (!game.StartCutscene("intro", &why)) {
      std::fprintf(stderr, "no intro: %s\n", why.c_str());
    }
  }
  mythos::FrameClock clock;
  mythos::Surface overlay(640, 360, 0);
  bool f5_was = false, f9_was = false;

  for (int frame = 0;; ++frame) {
    mythos::MoveInput move;
    mythos::UiInput ui;
    mythos::ActionInput act;
    const mythos::InputPoll poll =
        mythos::PollInput(move, ui, act, true, nullptr);
    if (poll.quit || (ui.pause_pressed && !controls_open)) break;

    // The controls page: F3 opens it; it rebinds, resets and saves.
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (controls_open) {
      const mythos::ControlsResult result = mythos::UpdateControlsPage(
          bindings, mythos::kPlayContext, controls, ui, poll.pressed);
      if (result == mythos::ControlsResult::kChanged &&
          !files.Write("bindings.json", bindings.ToJson().dump(2))) {
        std::fprintf(stderr, "could not save the key bindings\n");
      }
      if (result == mythos::ControlsResult::kBack) controls_open = false;
    } else if (keys[SDL_SCANCODE_F3] && !f3_was) {
      controls_open = true;
      controls = {};
    }
    f3_was = keys[SDL_SCANCODE_F3];
    actions.Update(
        bindings, mythos::kPlayContext,
        [](const mythos::Binding& b) { return mythos::ProbeBinding(b); });
    if (controls_open) {
      move = {};
      act = {};
    } else {
      mythos::ApplyActions(actions, move, act);
    }
    if (poll.toggle_fullscreen) window.ApplyMode(true, 1280, 720);

    if (keys[SDL_SCANCODE_F5] && !f5_was) game.Save(save_dir, 0);
    if (keys[SDL_SCANCODE_F9] && !f9_was) game.LoadSave(save_dir, 0);
    f5_was = keys[SDL_SCANCODE_F5];
    f9_was = keys[SDL_SCANCODE_F9];

    const double dt = std::min(clock.Step(false), 0.1);
    if (!controls_open) game.Update(dt, move, act);

    bool level_changed = false, textures_changed = false,
         shaders_changed = false;
    for (const std::string& path : watcher.Poll(dt)) {
      if (HasSuffix(path, ".txt")) level_changed = true;
      for (const char* ext : {".png", ".bmp", ".tga", ".jpg"}) {
        if (HasSuffix(path, ext)) textures_changed = true;
      }
      if (path.starts_with(EngineShaders())) shaders_changed = true;
    }
    if (level_changed || textures_changed || shaders_changed) {
      bool ok = true;
      if (shaders_changed) ok = renderer.ReloadShaders(EngineShaders()) && ok;
      if (textures_changed) {
        textures.LoadOverrides(GameAssets());
        renderer.ReloadTextures(textures);
      }
      if (level_changed) {
        std::string why;
        if (!game.ReloadLevel(&why)) {
          std::fprintf(stderr, "level reload: %s\n", why.c_str());
          ok = false;
        }
      }
      if (level_changed || textures_changed) {
        renderer.UploadWorld(
            mythos::BuildSceneGeometry(game.level(), game.models(), outdoor));
      }
      std::fprintf(stderr, "hot reload:%s%s%s%s\n",
                   level_changed ? " level" : "",
                   textures_changed ? " textures" : "",
                   shaders_changed ? " shaders" : "", ok ? "" : " (failed)");
      game.Say(ok ? "template.reloaded" : "template.reload_failed");
    }

    // The camera is the player's eye; lamps and doors come from the game.
    mythos::CameraState cam;
    mythos::ApplySettings(settings, cam);
    cam.x = game.player().pos.x;
    cam.z = game.player().pos.y;
    cam.yaw = game.player().angle;
    cam.pitch = game.player().pitch;
    // A cutscene takes the camera from the player.
    mythos::CameraSample film;
    if (game.CutsceneActive() && game.CutsceneCamera(&film)) {
      cam.x = film.pos.x;
      cam.z = film.pos.z;
      cam.eye_height = film.pos.y;
      cam.yaw = film.yaw;
      cam.pitch = film.pitch;
      cam.fov_scale = film.fov_scale;
    }
    cam.time = clock.Seconds();
    cam.outdoor = outdoor;
    if (outdoor)
      cam.time_of_day = std::fmod(hour + clock.Seconds() / 10.0, 24.0);
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
    // Far away she is not worth drawing (a cheaper model could stand in with
    // SceneRenderer::SetModelLod); her shadow is kept regardless.
    pete.max_distance = 30.0;
    PlaceMatrix(game.guide_pos().x, game.guide_pos().y, game.guide_angle(),
                pete.matrix);
    pete.skin = mythos::SkinMatrices(guide.skeleton, game.guide_pose());
    cam.instances.push_back(pete);

    // A little text on the overlay, and the cutscene's fade over everything.
    std::ranges::fill(overlay.px, 0u);
    if (game.CutsceneActive()) {
      const double fade = std::clamp(game.CutsceneValue("fade"), 0.0, 1.0);
      std::ranges::fill(overlay.px,
                        mythos::Rgba(0, 0, 0, static_cast<int>(fade * 255)));
    }
    mythos::DrawText(overlay, 8, 8, mythos::Tr("template.help"),
                     mythos::Rgba(255, 255, 255));
    text_layer.Clear();
    if (controls_open) {
      mythos::UiCanvas canvas{overlay, text_layer, nullptr};
      mythos::DrawControlsPage(canvas, theme, bindings, mythos::kPlayContext,
                               controls, clock.Seconds());
    } else if (!game.message().empty()) {
      const std::string line = mythos::Tr(game.message());
      mythos::DrawText(overlay, (overlay.w - mythos::TextWidth(line, 2)) / 2,
                       overlay.h - 40, line, mythos::Rgba(255, 230, 160), 2);
    }

    for (const std::string& cue : game.TakeSounds()) audio.Play(cue);
    if (hum >= 0) {
      const mythos::Vec2 ear = game.player().pos;
      const mythos::StereoGain pan = mythos::PositionalGain(
          ear, game.player().angle, hum_at, 0.0, false, 1.0, 0.08);
      const mythos::SoundBlockers between =
          mythos::CountSoundBlockers(game.level(), hum_at, ear);
      const mythos::Occlusion occluded =
          mythos::ComputeOcclusion(between.walls, between.closed_doors);
      const auto gain = static_cast<float>(occluded.gain);
      audio.SetSource(hum, pan.left * gain, pan.right * gain);
      audio.SetSourceMuffle(hum, static_cast<float>(occluded.muffle));
    }

    int w = 0, h = 0;
    window.DrawableSize(w, h);
    renderer.Render(cam, w, h, &overlay, &text_layer);
    if (!screenshot.empty() && frame == 90) {
      mythos::WriteFramebufferPpm(screenshot, w, h);
      break;
    }
    window.Swap();
  }
  window.Close();
  return 0;
}
