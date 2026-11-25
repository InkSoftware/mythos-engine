// The template game's front end: a window, input, the renderer and a loop. All
// the rules are in game.h (headless); this file only turns them into frames.
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "engine/audio/audio_math.h"
#include "engine/audio/spatial.h"
#include "engine/core/cvar.h"
#include "engine/core/file_watch.h"
#include "engine/core/fileio.h"
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
#include "engine/ui/debug_overlay.h"
#include "engine/ui/input_map.h"
#include "engine/ui/settings.h"
#include "engine/ui/settings_cvars.h"
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
  // `--debug` opens the debug console with the frame stats and graph on.
  // `--menu` opens the settings menu at the start.
  // `--controls` opens the menu on the controls page at the start (for a
  // screenshot).
  // `--crowd N` scatters N chairs as model instances (a stress test for
  // instancing; `--no-instancing` draws them one by one to compare).
  // `--reflections` captures a reflection probe in each room: shiny things
  // (the toolbox, the bucket) then reflect the room.
  // `--sun-shadow` (with `--outdoor`) adds the shadow of the sun or the moon.
  // `--baked` bakes the lamps into light probes instead of lighting with them
  // live (no shadows from them, no 32-lamp limit).
  // `--mirror` puts a mirror on a wall of the first room.
  // `--ssao` turns on ambient occlusion (the Ambient occlusion setting);
  // `--ssao-ambient` applies it to the ambient term in the world shader.
  // `--outdoor [--hour H]` takes the roof off and runs a day: the sun, sky
  // and light follow a time of day that moves on (ten seconds an hour).
  std::string screenshot;
  bool outdoor = false;
  bool ssao = false;
  bool ssao_ambient = false;
  bool mirror = false;
  bool baked = false;
  bool sun_shadow = false;
  bool reflections = false;
  int crowd = 0;
  int clones = 0;
  double world_lod = 0;  // --lod D: furniture is boxes beyond D metres
  bool instancing = true;
  bool start_controls = false;
  bool start_menu = false;
  bool start_debug = false;
  double hour = 20.0;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--outdoor") outdoor = true;
    if (arg == "--mirror") mirror = true;
    if (arg == "--ssao") ssao = true;
    if (arg == "--ssao-ambient") ssao = ssao_ambient = true;
    if (arg == "--baked") baked = true;
    if (arg == "--sun-shadow") sun_shadow = true;
    if (arg == "--reflections") reflections = true;
    if (arg == "--no-instancing") instancing = false;
    if (i + 1 < argc && arg == "--lod")
      world_lod = std::strtod(argv[i + 1], nullptr);
    if (i + 1 < argc && arg == "--clones")
      clones = static_cast<int>(std::strtol(argv[i + 1], nullptr, 10));
    if (i + 1 < argc && arg == "--crowd")
      crowd = static_cast<int>(std::strtol(argv[i + 1], nullptr, 10));
    if (arg == "--controls") start_controls = true;
    if (arg == "--menu") start_menu = true;
    if (arg == "--debug") start_debug = true;
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
    // The model's own colour texture when it has one, else a flat colour.
    const auto own = guide.textures.find(guide.batches[i].texture);
    textures.Add(guide.batches[i].texture,
                 own != guide.textures.end() ? own->second
                                             : FlatColor(guide.base_colors[i]));
  }

  mythos::SceneRenderer renderer;
  if (!renderer.Init(textures)) {
    std::fprintf(stderr, "the renderer could not start\n");
    return 1;
  }
  // The world, and with `--lod D` the same level built again from boxes for
  // the furniture, drawn for the columns of cells farther than D metres.
  const auto upload_world = [&] {
    renderer.UploadWorld(
        mythos::BuildSceneGeometry(game.level(), game.models(), outdoor));
    if (world_lod > 0) {
      renderer.UploadWorldLod(
          mythos::BuildSceneGeometry(game.level(),
                                     game.models().BoundingBoxes(), outdoor),
          world_lod);
    }
  };
  upload_world();
  renderer.UploadDoorPanel(mythos::BuildDoorPanel());
  const int guide_model = renderer.UploadModel(guide.batches);
  renderer.SetInstancing(instancing);
  if (reflections) {
    // One in the middle of each room, with the room's size so what a surface
    // reflects is parallax corrected.
    renderer.AddReflectionProbe(4.5, 1.25, 3.0, 3.5, 1.25, 2.0);   // first room
    renderer.AddReflectionProbe(14.0, 1.25, 3.0, 4.0, 1.25, 2.0);  // back room
  }
  int chair_model = -1;
  if (const mythos::Model* chair = game.models().Find("chair")) {
    chair_model = renderer.UploadModel(chair->batches);
  }

  // Baked light: the static lamps of the level become a probe grid, and are
  // then left out of the frame's live lights.
  // The probes come from the baked file `hall.probes` (made by the engine's
  // `mythos_bake_probes` from `hall.lamps`); a level edited while the game
  // runs is baked again here from the lamps of the level.
  const auto load_baked = [&] {
    mythos::ProbeStack grid;
    std::string why;
    if (mythos::ParseProbeStack(
            mythos::ReadTextFile(GameAssets() + "/levels/hall.probes"), &grid,
            &why)) {
      renderer.UploadProbes(grid);
      return true;
    }
    return false;
  };
  const auto bake = [&] {
    std::vector<mythos::ProbeLamp> lamps;
    for (const mythos::Vec3& p : game.LampPositions()) {
      lamps.push_back({p.x, p.y, p.z, 1.0, 0.78, 0.5, 7.0});
    }
    // Three heights (knee, chest, head): the light under a lamp differs.
    renderer.UploadProbes(
        mythos::BakeProbeStack(game.level(), lamps, {0.4, 1.2, 2.1}));
  };
  if (baked && !load_baked()) bake();

  const mythos::UserFiles files("mythos", "template-game");
  const std::string save_dir = files.Path("saves");

  // The player's settings, from settings.txt (the first run opens at 1280 x
  // 720). `controls_page` adds a Controls row to the menu.
  mythos::GameSettings settings = mythos::LoadSettings(files);
  if (files.Read("settings.txt").empty()) {
    settings.resolution = mythos::ResolutionIndex(1280, 720);
  }
  settings.controls_page = true;
  if (ssao) settings.ambient_occlusion = true;
  mythos::SetLanguage(settings.language);

  // Sound: the engine's common clips. A hum in the back room is a positional
  // source: panned and attenuated by distance, and muffled by the walls and
  // the closed door between it and the player (occlusion).
  mythos::AudioEngine audio;
  int hum = -1;
  mythos::Vec2 hum_at{14.5, 2.5};
  if (audio.Init()) {
    audio.LoadDir(EngineAssets() + "/sounds",
                  {"step_wood1", "step_wood2", "step_wood3", "door_open",
                   "door_close", "wind", "tick"});
    hum = audio.AddSource("wind");  // the "ambience" group
    // Layered music: two loops that start together, a layer fading in when
    // the player reaches the back room (stand-ins for real music stems).
    audio.StartMusic({"tick"});
    audio.SetMusicLayer("tick", 0.0f, 0.0f);
    // Door and step sounds ("effects") dip the ambience a little.
    audio.AddGroupDuck("ambience", "effects", 0.6f, 0.05f, 0.8f);
  }

  // Input actions: the engine's default keys, with the player's rebinding laid
  // over them from the `bindings` line of settings.txt.
  mythos::InputMap bindings = mythos::DefaultGameplayMap();
  {
    mythos::json::Value saved;
    if (!settings.bindings.empty() &&
        mythos::json::Parse(settings.bindings, saved)) {
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

  // The debug console (the ` key or F12): every setting is a variable
  // (`shadowMaps 3`), `debug.stats` and `debug.graph` show the frame cost, and
  // `cutscene.play intro` runs a cutscene. `help` lists everything.
  mythos::CvarRegistry cvars;
  mythos::RegisterSettingsCvars(cvars, settings);
  mythos::DebugOverlayOptions debug;
  mythos::RegisterDebugCvars(cvars, debug);
  mythos::CutsceneHooks hooks;
  hooks.start = [&](const std::string& name, std::string* why) {
    return game.StartCutscene(name, why);
  };
  hooks.skip = [&] { game.SkipCutscene(); };
  hooks.stop = [&] { game.StopCutscene(); };
  hooks.list = [&] {
    std::vector<std::string> names;
    for (const auto& entry :
         std::filesystem::directory_iterator(GameAssets() + "/cutscenes")) {
      if (entry.path().extension() == ".cutscene") {
        names.push_back(entry.path().stem().string());
      }
    }
    std::ranges::sort(names);
    return names;
  };
  mythos::RegisterCutsceneCommands(cvars, hooks);
  mythos::DebugConsole console(cvars);
  if (start_debug) {  // the console open, with the frame stats and graph on
    debug.stats = debug.graph = true;
    console.Show();
    console.Log("try: help, list, cutscene.play intro, debug.fly 1");
  }
  mythos::FrameHistory frames;
  mythos::DebugOverlayTheme debug_theme;
  debug_theme.text = theme.hint.Recolored(0xFFE6DCC8);
  // The settings menu (Esc) and, from its Controls row or F3, the controls
  // page. The game pauses while either is open.
  bool menu_open = start_controls || start_menu, f3_was = false;
  mythos::SettingsPage page = start_controls ? mythos::SettingsPage::kControls
                                             : mythos::SettingsPage::kMain;
  int menu_index = mythos::kSetGraphics;
  auto applied = mythos::ToKeyValues(settings);
  mythos::ApplySettings(settings, window, renderer, audio);

  // Hot reload (development): edit the level, a texture or a shader and see it
  // without restarting. The paths are the ones compiled in by CMake.
  mythos::FileWatcher watcher;
  watcher.Add(GameAssets());
  watcher.Add(EngineShaders());
  watcher.Add(EngineAssets() + "/models");
  watcher.Add(EngineAssets() + "/sounds");
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
  // A slammed door shakes the view a little, and tilts it (roll).
  mythos::CameraFx camera_fx;
  camera_fx.params.max_roll = 0.02;
  mythos::FrameClock clock;
  mythos::Surface overlay(640, 360, 0);
  bool f5_was = false, f9_was = false, was_in_back_room = false;

  for (int frame = 0;; ++frame) {
    mythos::MoveInput move;
    mythos::UiInput ui;
    mythos::ActionInput act;
    const mythos::InputPoll poll =
        mythos::PollInput(move, ui, act, true, nullptr);
    if (poll.quit) break;

    // The console: typing and editing keys while it is open.
    const mythos::ConsoleInput& typed = poll.console;
    if (typed.toggle) {
      console.Toggle();
      mythos::SetConsoleCapture(window, console.Visible());
    }
    if (console.Visible()) {
      if (!typed.text.empty()) console.Type(typed.text);
      if (typed.backspace) console.Backspace();
      if (typed.submit) console.Submit();
      if (typed.up) console.HistoryUp();
      if (typed.down) console.HistoryDown();
      if (typed.complete) console.Complete();
      if (typed.page_up) console.Scroll(5);
      if (typed.page_down) console.Scroll(-5);
    }

    // The menu: Esc opens the settings, F3 goes straight to the controls.
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (!menu_open) {
      if (ui.pause_pressed) {
        menu_open = true;
        page = mythos::SettingsPage::kMain;
        menu_index = mythos::kSetGraphics;
      } else if (keys[SDL_SCANCODE_F3] && !f3_was) {
        menu_open = true;
        page = mythos::SettingsPage::kControls;
        controls = {};
      }
    } else if (page == mythos::SettingsPage::kControls) {
      const mythos::ControlsResult result = mythos::UpdateControlsPage(
          bindings, mythos::kPlayContext, controls, ui, poll.pressed);
      if (result == mythos::ControlsResult::kChanged) {
        settings.bindings = bindings.ToJson().dump();
        mythos::SaveSettings(files, settings);
      }
      if (result == mythos::ControlsResult::kBack) {
        mythos::LeaveControlsPage(page, menu_index);
      }
    } else if (mythos::UpdateSettingsPage(
                   settings, page, menu_index, ui, false,
                   mythos::LargestFittingResolution(window))) {
      menu_open = false;
      mythos::SaveSettings(files, settings);
    }
    f3_was = keys[SDL_SCANCODE_F3];
    // A changed setting reaches the window, the renderer and the mixer.
    if (const auto now = mythos::ToKeyValues(settings); now != applied) {
      applied = now;
      mythos::ApplySettings(settings, window, renderer, audio);
    }
    actions.Update(
        bindings, mythos::kPlayContext,
        [](const mythos::Binding& b) { return mythos::ProbeBinding(b); });
    if (menu_open) {
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
    if (!menu_open) game.Update(dt, move, act);

    bool level_changed = false, textures_changed = false,
         shaders_changed = false, models_changed = false,
         sounds_changed = false;
    for (const std::string& path : watcher.Poll(dt)) {
      if (HasSuffix(path, ".txt")) level_changed = true;
      for (const char* ext : {".png", ".bmp", ".tga", ".jpg"}) {
        if (HasSuffix(path, ext)) textures_changed = true;
      }
      if (path.starts_with(EngineShaders())) shaders_changed = true;
      if (HasSuffix(path, ".obj")) {
        std::string why;
        if (game.ReloadModel(path, &why)) {
          models_changed = true;
        } else {
          std::fprintf(stderr, "model reload: %s\n", why.c_str());
        }
      }
      if (HasSuffix(path, ".wav")) {
        // A clip replaced in place: a sound that is playing carries on.
        const std::filesystem::path file(path);
        audio.LoadDir(file.parent_path().string(), {file.stem().string()});
        sounds_changed = true;
      }
    }
    if (level_changed || textures_changed || shaders_changed ||
        models_changed || sounds_changed) {
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
      if (level_changed && baked) bake();
      if (level_changed || textures_changed || models_changed) {
        upload_world();
      }
      std::fprintf(
          stderr, "hot reload:%s%s%s%s%s%s\n", level_changed ? " level" : "",
          textures_changed ? " textures" : "",
          shaders_changed ? " shaders" : "", models_changed ? " models" : "",
          sounds_changed ? " sounds" : "", ok ? "" : " (failed)");
      game.Say(ok ? "template.reloaded" : "template.reload_failed");
    }

    // The camera is the player's eye; lamps and doors come from the game.
    mythos::CameraState cam;
    mythos::ApplySettings(settings, cam);
    cam.x = game.player().pos.x;
    cam.z = game.player().pos.y;
    cam.yaw = game.player().angle;
    cam.pitch = game.player().pitch;
    // The shake and tilt ride on the player's camera (the cutscene has its
    // own).
    camera_fx.Update(dt);
    mythos::ApplyCameraFx(camera_fx.Sample(), cam);
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
    cam.sun_shadow = sun_shadow ? 1.0 : 0.0;
    cam.reflections = reflections ? 1.0 : 0.0;
    cam.ssao_ambient = ssao_ambient;
    if (mirror) {  // a mirror on the first room's north wall
      cam.mirror.on = true;
      cam.mirror.x = 5.5;
      cam.mirror.y = 1.3;
      cam.mirror.z = 1.02;
      cam.mirror.nx = 0;
      cam.mirror.nz = 1;
      cam.mirror.half_w = 0.9;
      cam.mirror.half_h = 0.7;
    }
    if (outdoor)
      cam.time_of_day = std::fmod(hour + clock.Seconds() / 10.0, 24.0);
    for (const mythos::Vec3& p :
         baked ? std::vector<mythos::Vec3>{} : game.LampPositions()) {
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
    // `--clones N`: the guide again, standing in the first room in her pose:
    // skinned instances of one model are one instanced draw per batch (the
    // bones go in a texture, a row each).
    for (int i = 0; i < clones; ++i) {
      mythos::ModelInstance clone = pete;
      clone.views = mythos::ModelInstance::kMain;
      PlaceMatrix(1.5 + 0.9 * (i % 8), 1.5 + 0.9 * (i / 8 % 4), i * 0.5,
                  clone.matrix);
      cam.instances.push_back(clone);
    }
    // The crowd: the same chair many times, a different spot and turn each.
    for (int i = 0; chair_model >= 0 && i < crowd; ++i) {
      mythos::ModelInstance chair;
      chair.model = chair_model;
      chair.views = mythos::ModelInstance::kMain;
      const double x = 1.5 + 7.0 * ((i * 37) % 101) / 101.0;
      const double z = 1.5 + 3.5 * ((i * 61) % 103) / 103.0;
      PlaceMatrix(x, z, i * 0.7, chair.matrix);
      cam.instances.push_back(chair);
    }

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
    frames.Add(dt);
    if (menu_open) {
      mythos::UiCanvas canvas{overlay, text_layer, nullptr};
      if (page == mythos::SettingsPage::kControls) {
        mythos::DrawControlsPage(canvas, theme, bindings, mythos::kPlayContext,
                                 controls, clock.Seconds());
      } else {
        mythos::DrawSettingsPage(
            canvas, theme, settings, page, menu_index,
            settings.language == mythos::Lang::kGreek ? "ΕΛΛΗΝΙΚΑ" : "ENGLISH",
            clock.Seconds());
      }
    } else if (!game.message().empty()) {
      const std::string line = mythos::Tr(game.message());
      mythos::DrawText(overlay, (overlay.w - mythos::TextWidth(line, 2)) / 2,
                       overlay.h - 40, line, mythos::Rgba(255, 230, 160), 2);
    }

    // Each sound has its own share of the room reverb: footsteps stay dry,
    // a door rings through the room.
    {
      mythos::UiCanvas debug_canvas{overlay, text_layer, nullptr};
      mythos::DrawDebugOverlay(debug_canvas, debug, debug_theme, frames,
                               renderer.Stats(), &console, clock.Seconds());
    }
    for (const std::string& cue : game.TakeSounds()) {
      if (cue == "door_close") camera_fx.AddShake(0.45);
      audio.Play(cue, 1.0f, 1.0f, "effects",
                 cue.starts_with("door") ? 1.5f : 0.4f);
    }
    // The back room brings the music layer in, leaving takes it out.
    if (const bool in_back_room = game.player().pos.x > 9.0;
        in_back_room != was_in_back_room) {
      was_in_back_room = in_back_room;
      // On the next bar line of a 100 bpm loop, so it comes in on the beat.
      audio.SetMusicLayerAtBar("tick", in_back_room ? 0.35f : 0.0f, 1.5f,
                               100.0);
    }
    // The room's reverb: the bigger the room the player stands in (the cells
    // reachable without a door), the longer the tail.
    const mythos::ReverbSend room =
        mythos::ReverbForRoom(mythos::RoomDoors::RegionSize(
            game.level(), static_cast<int>(game.player().pos.x),
            static_cast<int>(game.player().pos.y)));
    audio.SetReverb(static_cast<float>(room.send),
                    static_cast<float>(room.decay_seconds));
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
      std::fprintf(stderr, "draw calls: %d, triangles: %d\n",
                   renderer.Stats().draw_calls, renderer.Stats().triangles);
      break;
    }
    window.Swap();
  }
  window.Close();
  return 0;
}
