// Headless tests of the template game's rules (no window, no GL).
#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "engine/core/fileio.h"
#include "engine/editor/level_doc.h"
#include "engine/editor/prefab.h"
#include "engine/world/save_state.h"

namespace {
int g_failures = 0;
#define CHECK(cond)                                                 \
  do {                                                              \
    if (!(cond)) {                                                  \
      std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                 \
    }                                                               \
  } while (0)

bool LoadGame(template_game::Game& game) {
  std::string error;
  if (game.Load(TEMPLATE_ENGINE_ASSETS, TEMPLATE_GAME_ASSETS, &error)) {
    return true;
  }
  std::printf("load failed: %s\n", error.c_str());
  return false;
}

void Step(template_game::Game& game, const double seconds,
          const mythos::MoveInput& move = {},
          const mythos::ActionInput& act = {}) {
  const int frames = static_cast<int>(std::lround(seconds * 60));
  for (int i = 0; i < frames; ++i) game.Update(1.0 / 60, move, act);
}

void TestLoadsAndStarts() {
  template_game::Game game;
  if (!LoadGame(game)) return ++g_failures, void();
  CHECK(game.level().Width() == 20 && game.level().Height() == 7);
  CHECK(game.player().pos.x == 2.5 && game.player().pos.y == 5.5);
  CHECK(game.LampPositions().size() == 2);
  CHECK(game.guide_model().FindClip("Walk") != nullptr);
}

void TestWalkingAndWalls() {
  template_game::Game game;
  if (!LoadGame(game)) return ++g_failures, void();
  mythos::MoveInput back;
  back.back = true;  // facing +x, so backing up runs into the west wall
  Step(game, 3.0, back);
  CHECK(game.player().pos.x > 1.0);  // stopped by the wall, not through it
}

void TestDoorOpensAndTheTriggerSpeaks() {
  template_game::Game game;
  if (!LoadGame(game)) return ++g_failures, void();
  CHECK(!game.level().Passable(9, 3));  // the door starts closed
  // Stand west of the door, facing it, and use it.
  mythos::Player p = game.player();
  p.pos = {8.2, 3.5};
  p.angle = 0;  // facing the door (+x)
  mythos::SaveData data;
  data.Set("player", mythos::PlayerToJson(p));
  std::string dir =
      (std::filesystem::temp_directory_path() / "template_game_test").string();
  std::filesystem::remove_all(dir);
  CHECK(mythos::SaveSlots(dir, template_game::kSaveVersion).Save(0, data) ==
        mythos::SaveStatus::kOk);
  CHECK(game.LoadSave(dir, 0));
  mythos::ActionInput use;
  use.interact_pressed = true;
  Step(game, 1.0 / 60, {}, use);
  Step(game, 1.5);
  CHECK(game.level().Passable(9, 3));  // open
  const std::vector<std::string> sounds = game.TakeSounds();
  CHECK(std::ranges::count(sounds, "door_open") == 1);
  CHECK(game.TakeSounds().empty());
  // Walk through to the back room; its trigger fires once.
  mythos::MoveInput forward;
  forward.forward = true;
  Step(game, 3.0, forward);
  CHECK(game.player().pos.x > 10.0);
  CHECK(game.progress().Flag("seen_east"));
  std::filesystem::remove_all(dir);
}

void TestGuidePatrolsAndAnimates() {
  template_game::Game game;
  if (!LoadGame(game)) return ++g_failures, void();
  // Out of her sight (behind the wall, in the other room) she walks.
  mythos::Player p = game.player();
  p.pos = {15.5, 5.5};
  mythos::SaveData data;
  data.Set("player", mythos::PlayerToJson(p));
  const std::string dir =
      (std::filesystem::temp_directory_path() / "template_game_test2").string();
  CHECK(mythos::SaveSlots(dir, template_game::kSaveVersion).Save(0, data) ==
        mythos::SaveStatus::kOk);
  CHECK(game.LoadSave(dir, 0));
  const mythos::Vec2 before = game.guide_pos();
  Step(game, 4.0);
  const mythos::Vec2 after = game.guide_pos();
  CHECK(std::hypot(after.x - before.x, after.y - before.y) > 0.5);
  CHECK(game.level().Passable(static_cast<int>(after.x),
                              static_cast<int>(after.y)));
  CHECK(game.guide_pose().size() ==
        static_cast<size_t>(game.guide_model().skeleton.Count()));
  std::filesystem::remove_all(dir);
}

void TestGuideWavesWhenSheSeesYou() {
  template_game::Game game;
  if (!LoadGame(game)) return ++g_failures, void();
  // The player stands in her room, so sooner or later her patrol turns her
  // toward them: she waves once, then goes back to idle or walking.
  bool waved = false;
  for (int i = 0; i < 60 * 30 && !waved; ++i) {
    Step(game, 1.0 / 60);
    waved = game.guide_state() == "wave";
  }
  CHECK(waved);
  Step(game, 3.0);
  CHECK(game.guide_state() != "wave");
}

void TestPrefabPlacesIntoTheHall() {
  // The dining prefab (a table, two chairs, a bowl) placed in the back room,
  // turned a quarter: what the editor's Prefabs panel does.
  mythos::editor::Prefab prefab;
  std::string error;
  CHECK(mythos::editor::ParsePrefab(
      mythos::ReadTextFile(std::string(TEMPLATE_GAME_ASSETS) +
                           "/prefabs/dining.prefab"),
      &prefab, &error));
  mythos::editor::LevelDoc doc;
  CHECK(mythos::editor::ParseLevelDoc(
      mythos::ReadTextFile(std::string(TEMPLATE_GAME_ASSETS) +
                           "/levels/hall.txt"),
      &doc, &error));
  const size_t before = doc.furniture.size();
  const auto wall = [](const char c) { return c == '#'; };
  const mythos::editor::PlaceResult placed =
      mythos::editor::PlacePrefab(doc, prefab, 15, 1, 1, wall);
  CHECK(placed.furniture == 3 && placed.decor == 1 && placed.skipped == 0);
  CHECK(doc.furniture.size() == before + 3);
}

void TestIntroCutscenePlaysAndSkips() {
  template_game::Game game;
  if (!LoadGame(game)) return ++g_failures, void();
  std::string error;
  CHECK(game.StartCutscene("intro", &error));
  CHECK(game.CutsceneActive());
  const mythos::Vec2 start = game.player().pos;
  // Walking is ignored while it plays; the camera follows the file.
  mythos::MoveInput forward;
  forward.forward = true;
  Step(game, 1.0, forward);
  CHECK(game.CutsceneActive());
  CHECK(game.player().pos.x == start.x && game.player().pos.y == start.y);
  mythos::CameraSample cam;
  CHECK(game.CutsceneCamera(&cam) && cam.pos.x < 7.5 && cam.pos.x > 2.5);
  CHECK(game.CutsceneValue("fade") < 0.01);  // faded in after a second
  CHECK(game.message() == "template.intro");
  Step(game, 5.0);
  CHECK(!game.CutsceneActive());
  CHECK(game.progress().Flag("intro_seen"));
  Step(game, 1.0, forward);
  CHECK(game.player().pos.x != start.x || game.player().pos.y != start.y);

  // Interact skips it, and the cues still run.
  template_game::Game second;
  if (!LoadGame(second)) return ++g_failures, void();
  CHECK(second.StartCutscene("intro", &error));
  mythos::ActionInput skip;
  skip.interact_pressed = true;
  Step(second, 1.0 / 60, {}, skip);
  CHECK(!second.CutsceneActive());
  CHECK(second.progress().Flag("intro_seen"));
  CHECK(!second.StartCutscene("nope", &error) && !error.empty());
}

void TestSavedDoorsComeBack() {
  template_game::Game game;
  if (!LoadGame(game)) return ++g_failures, void();
  const std::string dir =
      (std::filesystem::temp_directory_path() / "template_game_test4").string();
  std::filesystem::remove_all(dir);
  // Open the door from the west side, wait for it, and save.
  mythos::Player p = game.player();
  p.pos = {8.2, 3.5};
  p.angle = 0;
  mythos::SaveData data;
  data.Set("player", mythos::PlayerToJson(p));
  CHECK(mythos::SaveSlots(dir, template_game::kSaveVersion).Save(1, data) ==
        mythos::SaveStatus::kOk);
  CHECK(game.LoadSave(dir, 1));
  mythos::ActionInput use;
  use.interact_pressed = true;
  Step(game, 1.0 / 60, {}, use);
  Step(game, 1.5);
  CHECK(game.level().Passable(9, 3));
  CHECK(game.Save(dir, 0));
  // A new game starts with the door shut and gets it open from the save.
  template_game::Game again;
  if (!LoadGame(again)) return ++g_failures, void();
  CHECK(!again.level().Passable(9, 3));
  CHECK(again.LoadSave(dir, 0));
  CHECK(again.level().Passable(9, 3));
  std::filesystem::remove_all(dir);
}

void TestGuideInvestigatesANoiseAndSearchesForYou() {
  template_game::Game game;
  if (!LoadGame(game)) return ++g_failures, void();
  // The player stands in the back room, far out of her sight; a noise in the
  // first room, in the middle of her patrol, brings her to it.
  mythos::Player p = game.player();
  p.pos = {15.5, 5.5};
  mythos::SaveData data;
  data.Set("player", mythos::PlayerToJson(p));
  const std::string dir =
      (std::filesystem::temp_directory_path() / "template_game_test5").string();
  CHECK(mythos::SaveSlots(dir, template_game::kSaveVersion).Save(0, data) ==
        mythos::SaveStatus::kOk);
  CHECK(game.LoadSave(dir, 0));
  game.MakeNoise({4.5, 3.5}, 14.0);
  double closest = 1e9;
  for (int i = 0; i < 60 * 8; ++i) {
    Step(game, 1.0 / 60);
    const mythos::Vec2 g = game.guide_pos();
    closest = std::min(closest, std::hypot(g.x - 4.5, g.y - 3.5));
  }
  CHECK(closest < 0.6);  // she walked right to it
  std::filesystem::remove_all(dir);
}

void TestSaveRoundTrip() {
  template_game::Game game;
  if (!LoadGame(game)) return ++g_failures, void();
  const std::string dir =
      (std::filesystem::temp_directory_path() / "template_game_test3").string();
  std::filesystem::remove_all(dir);
  CHECK(!game.LoadSave(dir, 0));  // nothing saved yet
  CHECK(game.message() == "template.no_save");
  mythos::MoveInput forward;
  forward.forward = true;
  Step(game, 1.0, forward);
  const mythos::Vec2 where = game.player().pos;
  CHECK(game.Save(dir, 0));
  Step(game, 1.0, forward);
  CHECK(game.LoadSave(dir, 0));
  CHECK(std::fabs(game.player().pos.x - where.x) < 1e-9);
  std::filesystem::remove_all(dir);
}
}

int main() {
  TestLoadsAndStarts();
  TestWalkingAndWalls();
  TestDoorOpensAndTheTriggerSpeaks();
  TestGuidePatrolsAndAnimates();
  TestGuideWavesWhenSheSeesYou();
  TestPrefabPlacesIntoTheHall();
  TestIntroCutscenePlaysAndSkips();
  TestSavedDoorsComeBack();
  TestGuideInvestigatesANoiseAndSearchesForYou();
  TestSaveRoundTrip();
  std::printf("%d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
