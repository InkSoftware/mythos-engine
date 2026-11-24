#include "game.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "engine/core/fileio.h"
#include "engine/core/json.h"
#include "engine/world/save_state.h"

namespace template_game {
namespace {
using mythos::ai::Status;

constexpr double kGuideSpeed = 1.1;  // cells per second
constexpr double kGuideSight = 6.0;  // metres
constexpr double kGuideFov = 2.2;    // radians
constexpr double kMessageSeconds = 4.0;

std::string Join(const std::string& dir, const std::string& rest) {
  return dir + "/" + rest;
}
}

Game::Game() : anim_(&guide_.skeleton, &guide_.clips) {
  params_.radius = 0.25;
  params_.walk_speed = 2.2;
  params_.run_speed = 4.0;
  params_.turn_speed = 2.2;
  params_.stride_length = 1.6;
  params_.mouse_sensitivity = 0.0025;
  params_.max_pitch = 1.2;
}

bool Game::Load(const std::string& engine_assets,
                const std::string& game_assets, std::string* error) {
  const auto fail = [&](const std::string& why) {
    if (error != nullptr) *error = why;
    return false;
  };
  // Models: the engine's, then the game's, which replace any of the same name.
  if (!models_.LoadDir(Join(engine_assets, "models"), error)) return false;
  game_assets_ = game_assets;
  models_.LoadDir(Join(game_assets, "models"));  // the game may have none

  mythos::json::Value legend;
  std::string why;
  if (!mythos::json::ParseFile(Join(game_assets, "data/tiles.json"), legend,
                               &why)) {
    return fail(why);
  }
  auto tiles = std::make_shared<mythos::TileSet>();
  if (!mythos::ParseTileSet(legend, *tiles, &why)) return fail(why);
  if (!level_.LoadFile(Join(game_assets, "levels/hall.txt"), &why)) {
    return fail(why);
  }
  level_.SetTiles(tiles);
  baseline_ = level_;
  doors_.Scan(level_);
  if (!events_.Load(level_))
    return fail("the level's triggers or rules are bad");

  if (!mythos::LoadGlb(Join(engine_assets, "characters/prototype_pete.glb"),
                       guide_, &why)) {
    return fail(why);
  }
  // A glTF file does not say which clips loop: they all load looping, and the
  // game marks the one-shots.
  for (mythos::Clip& clip : guide_.clips) {
    if (clip.name == "Wave") clip.loop = false;
  }
  // Her animation: idle and walk follow `moving`; a wave plays once when she
  // first sees the player and then returns to idle by itself.
  anim_.AddState("idle", "Idle");
  anim_.AddState("walk", "Walk");
  anim_.AddState("wave", "Wave");
  // First: transitions are tried in order, and the greeting lasts one frame.
  anim_.AddTransition({"*", "wave", {{"greet", mythos::AnimOp::kEqual, 1}}});
  anim_.AddTransition(
      {"idle", "walk", {{"moving", mythos::AnimOp::kEqual, 1}}});
  anim_.AddTransition(
      {"walk", "idle", {{"moving", mythos::AnimOp::kEqual, 0}}});
  anim_.AddTransition({"wave", "idle", {}, true});
  Restart();
  return true;
}

bool Game::ReloadLevel(std::string* error) {
  mythos::Level fresh;
  std::string why;
  if (!fresh.LoadFile(Join(game_assets_, "levels/hall.txt"), &why)) {
    if (error != nullptr) *error = why;
    return false;
  }
  fresh.SetTiles(level_.TilesPtr());
  mythos::EventGraphRuntime events;
  if (!events.Load(fresh)) {
    if (error != nullptr) *error = "the level's triggers or rules are bad";
    return false;
  }
  level_ = std::move(fresh);
  baseline_ = level_;
  doors_.Scan(level_);
  events_.Load(level_);
  // Where the guide walks may have changed too.
  waypoints_.clear();
  for (const auto& [x, y] : level_.FindAll('w')) waypoints_.push_back({x, y});
  route_.clear();
  route_step_ = 0;
  next_waypoint_ = 0;
  if (!level_.Passable(static_cast<int>(player_.pos.x),
                       static_cast<int>(player_.pos.y))) {
    int sx = 1, sy = 1;
    level_.Find('@', sx, sy);
    player_.pos = {sx + 0.5, sy + 0.5};
  }
  return true;
}

void Game::Restart() {
  int sx = 1, sy = 1;
  level_.Find('@', sx, sy);
  start_ = {sx + 0.5, sy + 0.5};
  player_ = {};
  player_.pos = start_;
  walker_.Reset();
  progress_ = {};
  message_.clear();
  events_.Reset();
  noises_.Clear();
  last_seen_.Forget();

  waypoints_.clear();
  for (const auto& [x, y] : level_.FindAll('w')) waypoints_.push_back({x, y});
  route_.clear();
  route_step_ = 0;
  next_waypoint_ = 0;
  if (!waypoints_.empty()) {
    guide_pos_ = {waypoints_[0].x + 0.5, waypoints_[0].y + 0.5};
  }
  guide_angle_ = 0;
  // Start by looking at the guide.
  player_.angle =
      std::atan2(guide_pos_.y - player_.pos.y, guide_pos_.x - player_.pos.x);

  // The guide's mind. A selector tries each child in turn: greet the player
  // when they are in sight, else walk the patrol. Leaves are ours; the engine
  // only walks the tree.
  using namespace mythos::ai;
  std::vector<NodePtr> greet;
  greet.push_back(Condition([this] { return guide_sees_player_; }));
  greet.push_back(Leaf([this](double) {
    guide_moving_ = false;
    guide_angle_ =
        std::atan2(player_.pos.y - guide_pos_.y, player_.pos.x - guide_pos_.x);
    return Status::kSuccess;
  }));
  // Looking for you: when she has lost sight of you, she goes to where she
  // last saw you.
  std::vector<NodePtr> search;
  search.push_back(Condition([this] {
    return !guide_sees_player_ && last_seen_.Remembers(time_, 6.0);
  }));
  search.push_back(Leaf([this](double dt) {
    const mythos::Vec2 at = last_seen_.Where();
    const Walk walk =
        WalkTo({static_cast<int>(at.x), static_cast<int>(at.y)}, dt, false);
    if (walk == Walk::kWalking) return Status::kRunning;
    last_seen_.Forget();  // arrived (or no way there): give up
    return Status::kFailure;
  }));
  // What she hears: a door opening brings her to look.
  std::vector<NodePtr> investigate;
  investigate.push_back(Condition([this] {
    heard_ = noises_.Loudest(level_, guide_pos_, 1.0, time_);
    return heard_.has_value();
  }));
  investigate.push_back(Leaf([this](double dt) {
    if (!heard_) return Status::kFailure;
    const Walk walk = WalkTo(
        {static_cast<int>(heard_->pos.x), static_cast<int>(heard_->pos.y)}, dt,
        false);
    return walk == Walk::kWalking ? Status::kRunning : Status::kFailure;
  }));
  std::vector<NodePtr> top;
  top.push_back(Sequence(std::move(greet)));
  top.push_back(Sequence(std::move(search)));
  top.push_back(Sequence(std::move(investigate)));
  top.push_back(Leaf([this](double dt) {
    // The patrol: walk to the next waypoint, then pick the next one.
    if (waypoints_.empty()) return Status::kFailure;
    const Walk walk = WalkTo(waypoints_[next_waypoint_], dt, true);
    if (walk == Walk::kArrived) {
      next_waypoint_ = (next_waypoint_ + 1) % waypoints_.size();
    } else if (walk == Walk::kNoRoute) {
      return Status::kFailure;
    }
    return Status::kRunning;
  }));
  brain_ = Selector(std::move(top));
}

// Walks the guide along a smoothed route to `goal` (planned again when the goal
// changes), turning her to face where she goes.
Game::Walk Game::WalkTo(const mythos::NavCell& goal, const double dt,
                        const bool avoid_doors) {
  const mythos::NavCell here{static_cast<int>(guide_pos_.x),
                             static_cast<int>(guide_pos_.y)};
  if (!(route_goal_ == goal) || route_step_ >= route_.size()) {
    if (here == goal) {
      route_.clear();
      route_step_ = 0;
      route_goal_ = {-1, -1};
      return Walk::kArrived;
    }
    mythos::NavOptions options;
    options.avoid_doors = avoid_doors;
    const std::vector<mythos::NavCell> cells =
        mythos::FindPath(level_, here, goal, options);
    if (cells.empty()) {
      route_.clear();
      route_goal_ = {-1, -1};
      return Walk::kNoRoute;
    }
    route_ = mythos::SmoothPath(level_, cells, 0.25);
    route_goal_ = goal;
    route_step_ = route_.size() > 1 ? 1 : 0;  // the first point is where she is
  }
  const mythos::Vec2 target = route_[route_step_];
  const double dx = target.x - guide_pos_.x, dy = target.y - guide_pos_.y;
  const double dist = std::hypot(dx, dy);
  const double step = kGuideSpeed * dt;
  if (dist <= step) {
    guide_pos_ = target;
    ++route_step_;
  } else {
    guide_pos_.x += dx / dist * step;
    guide_pos_.y += dy / dist * step;
    guide_angle_ = std::atan2(dy, dx);
  }
  guide_moving_ = true;
  if (route_step_ >= route_.size()) {
    route_goal_ = {-1, -1};
    return Walk::kArrived;
  }
  return Walk::kWalking;
}

void Game::Say(const std::string& key) {
  message_ = key;
  message_time_ = kMessageSeconds;
}

std::vector<mythos::Vec3> Game::LampPositions() const {
  std::vector<mythos::Vec3> out;
  for (const auto& [x, y] : level_.FindAll('L')) {
    out.push_back({x + 0.5, 2.1, y + 0.5});
  }
  return out;
}

void Game::UseDoorAhead() {
  // Look for a door a step or two in front of the player.
  for (const double reach : {0.8, 1.3}) {
    const int x = static_cast<int>(
        std::floor(player_.pos.x + std::cos(player_.angle) * reach));
    const int y = static_cast<int>(
        std::floor(player_.pos.y + std::sin(player_.angle) * reach));
    if (!level_.InBounds(x, y) || !level_.Tiles().IsDoor(level_.At(x, y))) {
      continue;
    }
    if (const int swing = doors_.Use(x, y); swing != 0) {
      sounds_.emplace_back(swing > 0 ? "door_open" : "door_close");
      // A door is heard through the house: the guide goes to look.
      MakeNoise({x + 0.5, y + 0.5}, 9.0);
      return;
    }
  }
}

void Game::RunEvents(const bool use_pressed) {
  mythos::EventGraphRuntime::Flags flags;
  for (const auto& [name, value] : progress_.flags) flags[name] = value != 0;
  events_.Sample({player_.pos.x, 0.0, player_.pos.y}, 0, use_pressed, flags,
                 [this](const mythos::EventAction& action) { Act(action); });
}

void Game::Act(const mythos::EventAction& action) {
  // The action types are ours: the engine only carries them.
  if (action.type == "set_flag") {
    progress_.flags[action.argument] = 1;
  } else if (action.type == "message") {
    Say(action.argument);
  }
}

void Game::SkipCutscene() {
  cutscene_player_.Skip([this](const mythos::EventAction& a) { Act(a); },
                        [this](const mythos::AnimKey& k) {
                          if (k.actor == "guide") cutscene_wave_ = true;
                        });
}

bool Game::StartCutscene(const std::string& name, std::string* error) {
  const std::string path =
      Join(game_assets_, "cutscenes/" + name + ".cutscene");
  const std::string text = mythos::ReadTextFile(path);
  std::string why;
  if (text.empty() || !mythos::ParseCutscene(text, &cutscene_, &why)) {
    if (error != nullptr) *error = text.empty() ? "cannot read " + path : why;
    return false;
  }
  cutscene_.name = name;
  cutscene_player_.Start(&cutscene_);
  return true;
}

void Game::UpdateGuide(const double dt) {
  mythos::ai::Sight sight;
  sight.eye = guide_pos_;
  sight.angle = guide_angle_;
  sight.fov = kGuideFov;
  sight.range = kGuideSight;
  guide_sees_player_ = mythos::ai::CanSee(level_, sight, player_.pos);
  if (guide_sees_player_) last_seen_.Saw(player_.pos, time_);
  guide_moving_ = false;
  if (brain_) brain_->Tick(dt);
  anim_.SetBool("moving", guide_moving_);
  anim_.SetBool("greet",
                (guide_sees_player_ && !guide_saw_player_) || cutscene_wave_);
  cutscene_wave_ = false;
  guide_saw_player_ = guide_sees_player_;
  anim_.Update(dt);
}

void Game::Update(const double dt, const mythos::MoveInput& move,
                  const mythos::ActionInput& act) {
  dt_ = dt;
  time_ += dt;
  if (cutscene_player_.Playing()) {
    // The world carries on, the player watches: the guide still acts, the
    // cues and animation keys are played, and interact skips.
    const auto cue = [this](const mythos::EventAction& a) { Act(a); };
    const auto anim = [this](const mythos::AnimKey& k) {
      if (k.actor == "guide") cutscene_wave_ = true;
    };
    if (act.interact_pressed) {
      cutscene_player_.Skip(cue, anim);
    } else {
      cutscene_player_.Update(dt, cue, anim);
    }
    UpdateGuide(dt);
    message_time_ = std::max(0.0, message_time_ - dt);
    if (message_time_ == 0) message_.clear();
    return;
  }
  mythos::ApplyLook(player_, move, params_);
  if (walker_.Update(level_, player_, dt, move, params_)) {
    static const char* const kSteps[] = {"step_wood1", "step_wood2",
                                         "step_wood3"};
    sounds_.emplace_back(kSteps[step_++ % 3]);
  }
  if (act.interact_pressed) UseDoorAhead();
  doors_.Update(level_, dt, player_.pos, params_.radius, 0.8, 0.8);
  RunEvents(act.interact_pressed);
  UpdateGuide(dt);
  message_time_ = std::max(0.0, message_time_ - dt);
  if (message_time_ == 0) message_.clear();
}

bool Game::Save(const std::string& dir, const int slot) {
  mythos::SaveData data;
  data.Set("player", mythos::PlayerToJson(player_));
  data.Set("progress", mythos::ProgressToJson(progress_));
  // Doors flip glyphs in the level: the changed cells are the saved doors.
  data.Set("level", mythos::LevelDiffToJson(baseline_, level_));
  data.Set("furniture", mythos::FurnitureDiffToJson(baseline_, level_));
  mythos::json::Value guide = mythos::json::Value::object();
  guide["x"] = guide_pos_.x;
  guide["z"] = guide_pos_.y;
  guide["angle"] = guide_angle_;
  data.Set("guide", std::move(guide));
  const bool ok = mythos::SaveSlots(dir, kSaveVersion).Save(slot, data) ==
                  mythos::SaveStatus::kOk;
  Say(ok ? "template.saved" : "template.no_save");
  return ok;
}

bool Game::LoadSave(const std::string& dir, const int slot) {
  mythos::SaveData data;
  if (mythos::SaveSlots(dir, kSaveVersion).Load(slot, data) !=
      mythos::SaveStatus::kOk) {
    Say("template.no_save");
    return false;
  }
  // A later version would add migrations here: SaveSlots takes a list.
  if (const auto* p = data.Section("player"))
    mythos::PlayerFromJson(*p, player_);
  if (const auto* p = data.Section("progress")) {
    mythos::ProgressFromJson(*p, progress_);
  }
  // Start from the level as loaded, then lay the saved changes over it.
  level_ = baseline_;
  if (const auto* d = data.Section("level")) mythos::ApplyLevelDiff(level_, *d);
  if (const auto* d = data.Section("furniture")) {
    mythos::ApplyFurnitureDiff(level_, *d);
  }
  doors_.Scan(level_);
  if (const auto* g = data.Section("guide")) {
    guide_pos_.x = g->value("x", guide_pos_.x);
    guide_pos_.y = g->value("z", guide_pos_.y);
    guide_angle_ = g->value("angle", guide_angle_);
    route_.clear();
    route_step_ = 0;
  }
  // The trigger runtime treats the player as outside until the next sample.
  events_.Reset();
  Say("template.loaded");
  return true;
}
}
