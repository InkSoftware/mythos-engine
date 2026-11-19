#ifndef MYTHOS_ENGINE_WORLD_ROOM_DOORS_H_
#define MYTHOS_ENGINE_WORLD_ROOM_DOORS_H_
// Plain swinging doors on the doorways of a grid level. A closed door is the
// glyph 'N' (solid), an open one 'n'. Each swings into the smaller of the two
// spaces it separates, turns solid half way through closing, and never shuts on
// a body standing in it.
#include <vector>

#include "engine/core/math.h"
#include "engine/world/level.h"

namespace mythos {

// One animated door for the renderer: where it is, which way you walk through
// it, which way it swings and how far open it is. A door is hinged on the frame
// face it swings towards, so it opens a quarter turn flat against the frame and
// never into the wall.
struct DoorState {
  int x = 0, y = 0;
  bool passage_along_x = false;  // you walk through along x (else along z)
  double open = 0;               // 0 closed .. 1 fully open
  int swing =
      1;  // +1 swings towards +x / +z along the passage, -1 the other way
};

class RoomDoors {
 public:
  // Collects every door of the level, all at rest (open where the map says
  // 'n').
  void Scan(const Level& level);
  void Clear() { doors_.clear(); }
  // Toggles the door at (x, y). Returns +1 if it began to open, -1 if it began
  // to close, 0 if there is no such door.
  int Use(int x, int y);
  // Swings the doors. Returns how many finished closing this call (for a
  // sound).
  int Update(Level& level, double dt, const Vec2& body, double body_radius,
             double open_seconds, double close_seconds);
  void AppendStates(std::vector<DoorState>& out) const;

  // The number of passable cells reachable from (x, y) without passing a door,
  // up to a cap.
  static int Depth(const Level& level, int x, int y, int step_x, int step_y);
  static int RegionSize(const Level& level, int x, int y);
  // True if a body of this radius at pos overlaps the cell.
  static bool Overlaps(const Vec2& pos, double radius, int x, int y);

 private:
  struct Door {
    int x = 0, y = 0;
    bool along_x = false;
    double open = 0;
    bool opening = false;
    int swing = 1;
  };
  std::vector<Door> doors_;
};

}  // namespace mythos

#endif  // MYTHOS_ENGINE_WORLD_ROOM_DOORS_H_
