// The rules of the template game: no SDL, no GL, so they are tested headless.
// The engine gives mechanisms; this class is the "meaning" a game supplies. It
// owns a level, the player, the doors, a guide character who patrols and the
// flags, and turns the engine's events into messages. Copy it and change it.
#ifndef TEMPLATE_GAME_GAME_H_
#define TEMPLATE_GAME_GAME_H_

#include <string>
#include <utility>
#include <vector>

#include "engine/data/save.h"
#include "engine/gfx/anim_state.h"
#include "engine/gfx/gltf.h"
#include "engine/gfx/model.h"
#include "engine/gfx/skeleton.h"
#include "engine/world/ai.h"
#include "engine/world/event_graph.h"
#include "engine/world/interact.h"
#include "engine/world/level.h"
#include "engine/world/nav.h"
#include "engine/world/player.h"
#include "engine/world/room_doors.h"

namespace template_game {
// Bump when the save layout changes and add a migration in game.cpp.
constexpr int kSaveVersion = 1;

class Game {
 public:
  Game();

  // Loads the level, the tile legend, the models and the guide from the asset
  // folders (the game's laid over the engine's). Returns false with the reason.
  bool Load(const std::string& engine_assets, const std::string& game_assets,
            std::string* error);

  // One step of the world.
  void Update(double dt, const mythos::MoveInput& move,
              const mythos::ActionInput& act);

  // Reads the level file again (development: the file changed on disk). The
  // player stays where they are unless that is now inside a wall; doors start
  // closed as the file says. A file that does not parse changes nothing.
  bool ReloadLevel(std::string* error);

  bool Save(const std::string& dir, int slot);
  // Returns false (and says why in the message) when there is nothing to load.
  bool LoadSave(const std::string& dir, int slot);

  [[nodiscard]] const mythos::Level& level() const { return level_; }
  [[nodiscard]] const mythos::ModelLibrary& models() const { return models_; }
  [[nodiscard]] const mythos::Player& player() const { return player_; }
  [[nodiscard]] const mythos::RoomDoors& doors() const { return doors_; }
  [[nodiscard]] const mythos::Progress& progress() const { return progress_; }
  [[nodiscard]] const mythos::GltfModel& guide_model() const { return guide_; }
  // Where the guide stands and which way she faces (radians, the player's
  // convention).
  [[nodiscard]] mythos::Vec2 guide_pos() const { return guide_pos_; }
  [[nodiscard]] double guide_angle() const { return guide_angle_; }
  [[nodiscard]] mythos::Pose guide_pose() const { return anim_.CurrentPose(); }
  // The guide's animation state: idle, walk or wave.
  [[nodiscard]] const std::string& guide_state() const { return anim_.State(); }
  // The line to show, a translation key, and how long it has left.
  [[nodiscard]] const std::string& message() const { return message_; }
  [[nodiscard]] double message_time() const { return message_time_; }
  // Where lamps stand: the cells of 'L', as world positions.
  [[nodiscard]] std::vector<mythos::Vec3> LampPositions() const;

  // The sound cues (clip names) raised since the last call: footsteps and
  // doors. The front end plays them; the rules only say which.
  std::vector<std::string> TakeSounds() { return std::exchange(sounds_, {}); }

  // Shows `key` for a few seconds.
  void Say(const std::string& key);

  // The player's parameters.
  [[nodiscard]] const mythos::PlayerParams& params() const { return params_; }

 private:
  void UpdateGuide(double dt);
  void RunEvents(bool use_pressed);
  void UseDoorAhead();
  void Restart();

  mythos::Level level_;
  mythos::ModelLibrary models_;
  mythos::Player player_;
  mythos::PlayerParams params_;
  mythos::PlayerWalker walker_;
  mythos::RoomDoors doors_;
  mythos::Progress progress_;
  mythos::EventGraphRuntime events_;

  // The guide: a skinned, animated character walking a patrol with a
  // behaviour tree.
  mythos::GltfModel guide_;
  mythos::AnimStateMachine anim_;
  mythos::ai::NodePtr brain_;
  std::vector<mythos::NavCell> waypoints_, path_;
  size_t next_waypoint_ = 0, path_step_ = 0;
  mythos::Vec2 guide_pos_;
  double guide_angle_ = 0;
  bool guide_sees_player_ = false;
  bool guide_moving_ = false;
  bool guide_saw_player_ = false;
  double dt_ = 0;

  std::vector<std::string> sounds_;
  int step_ = 0;
  std::string message_;
  double message_time_ = 0;
  mythos::Vec2 start_;
  std::string game_assets_;
};
}

#endif
