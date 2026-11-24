#include "game.h"

#include <algorithm>
#include <cmath>
#include <memory>

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

  waypoints_.clear();
  for (const auto& [x, y] : level_.FindAll('w')) waypoints_.push_back({x, y});
  path_.clear();
  path_step_ = 0;
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
  std::vector<NodePtr> top;
  top.push_back(Sequence(std::move(greet)));
  top.push_back(Leaf([this](double dt) {
    // Walk the cells of a path to the next waypoint, then pick the next one.
    if (waypoints_.empty()) return Status::kFailure;
    if (path_step_ >= path_.size()) {
      const mythos::NavCell here{static_cast<int>(guide_pos_.x),
                                 static_cast<int>(guide_pos_.y)};
      next_waypoint_ = (next_waypoint_ + 1) % waypoints_.size();
      mythos::NavOptions options;
      options.avoid_doors = true;
      path_ =
          mythos::FindPath(level_, here, waypoints_[next_waypoint_], options);
      path_step_ = 0;
      if (path_.empty()) return Status::kFailure;
    }
    const mythos::NavCell cell = path_[path_step_];
    const double tx = cell.x + 0.5, ty = cell.y + 0.5;
    const double dx = tx - guide_pos_.x, dy = ty - guide_pos_.y;
    const double dist = std::hypot(dx, dy);
    const double step = kGuideSpeed * dt;
    if (dist <= step) {
      guide_pos_ = {tx, ty};
      ++path_step_;
    } else {
      guide_pos_.x += dx / dist * step;
      guide_pos_.y += dy / dist * step;
      guide_angle_ = std::atan2(dy, dx);
    }
    guide_moving_ = true;
    return Status::kRunning;
  }));
  brain_ = Selector(std::move(top));
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
      return;
    }
  }
}

void Game::RunEvents(const bool use_pressed) {
  mythos::EventGraphRuntime::Flags flags;
  for (const auto& [name, value] : progress_.flags) flags[name] = value != 0;
  events_.Sample({player_.pos.x, 0.0, player_.pos.y}, 0, use_pressed, flags,
                 [this](const mythos::EventAction& action) {
                   // The action types are ours: the engine only carries them.
                   if (action.type == "set_flag") {
                     progress_.flags[action.argument] = 1;
                   } else if (action.type == "message") {
                     Say(action.argument);
                   }
                 });
}

void Game::UpdateGuide(const double dt) {
  mythos::ai::Sight sight;
  sight.eye = guide_pos_;
  sight.angle = guide_angle_;
  sight.fov = kGuideFov;
  sight.range = kGuideSight;
  guide_sees_player_ = mythos::ai::CanSee(level_, sight, player_.pos);
  guide_moving_ = false;
  if (brain_) brain_->Tick(dt);
  anim_.SetBool("moving", guide_moving_);
  anim_.SetBool("greet", guide_sees_player_ && !guide_saw_player_);
  guide_saw_player_ = guide_sees_player_;
  anim_.Update(dt);
}

void Game::Update(const double dt, const mythos::MoveInput& move,
                  const mythos::ActionInput& act) {
  dt_ = dt;
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
  // Doors flip glyphs in the level, so remember each door cell's glyph.
  mythos::json::Value doors = mythos::json::Value::array();
  for (int y = 0; y < level_.Height(); ++y) {
    for (int x = 0; x < level_.Width(); ++x) {
      if (level_.Tiles().IsDoor(level_.At(x, y))) {
        doors.push_back({x, y, std::string(1, level_.At(x, y))});
      }
    }
  }
  data.Set("doors", std::move(doors));
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
  if (const auto* d = data.Section("doors"); d != nullptr && d->is_array()) {
    for (const auto& item : *d) {
      if (item.is_array() && item.size() == 3 && item[2].is_string() &&
          !item[2].get<std::string>().empty()) {
        level_.Set(item[0].get<int>(), item[1].get<int>(),
                   item[2].get<std::string>()[0]);
      }
    }
    doors_.Scan(level_);
  }
  if (const auto* g = data.Section("guide")) {
    guide_pos_.x = g->value("x", guide_pos_.x);
    guide_pos_.y = g->value("z", guide_pos_.y);
    guide_angle_ = g->value("angle", guide_angle_);
    path_.clear();
    path_step_ = 0;
  }
  // The trigger runtime treats the player as outside until the next sample.
  events_.Reset();
  Say("template.loaded");
  return true;
}
}
