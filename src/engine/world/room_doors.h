#ifndef MYTHOS_ENGINE_WORLD_ROOM_DOORS_H_
#define MYTHOS_ENGINE_WORLD_ROOM_DOORS_H_

#include <vector>

#include "engine/core/math.h"
#include "engine/world/level.h"

// Plain swinging doors on the doorways of a grid level. A closed door is the
// glyph 'N' (solid), an open one 'n'. Each swings into the smaller of the two
// spaces it separates, turns passable half way through opening, and never shuts
// on a body standing in it.
namespace mythos {
// One animated door for the renderer: where it is, which way you walk through
// it, which way it swings and how far open it is. A door is hinged on the frame
// face it swings toward, so it opens a quarter turn flat against the frame and
// never into the wall.
struct DoorState {
  int x = 0, y = 0;
  // You walk through along x (else along z).
  bool passage_along_x = false;
  // 0 closed to 1 fully open.
  double open = 0;
  // +1 swings toward +x / +z along the passage, -1 the other way.
  int swing = 1;
};

// Owns the animation state of every door of one level. Mutates the level's
// glyphs through Update, so call Scan again after loading another level.
class RoomDoors {
 public:
  // Collects every door of the level, all at rest (open where the map says
  // 'n'), and works out which way each swings.
  void Scan(const Level& level);

  // Forgets all doors.
  void Clear() { doors_.clear(); }

  // Toggles the door at (x, y). Returns +1 if it began to open, -1 if it began
  // to close, 0 if there is no such door.
  int Use(int x, int y);

  // Swings the doors for dt seconds (a full swing takes open_seconds or
  // close_seconds) and flips the level glyph at the half-open point. A closing
  // door waits ajar while a body of body_radius at body overlaps its cell.
  // Returns how many finished closing this call (for a sound).
  int Update(Level& level, double dt, const Vec2& body, double body_radius,
             double open_seconds, double close_seconds);

  // Appends one DoorState per door to out.
  void AppendStates(std::vector<DoorState>& out) const;

  // Number of bare cells running straight from (x, y) in the step direction
  // before a wall or door, up to a cap.
  static int Depth(const Level& level, int x, int y, int step_x, int step_y);

  // Number of passable cells reachable from (x, y) without passing a door, up
  // to a cap (also returned for a blocked start cell).
  static int RegionSize(const Level& level, int x, int y);

  // True if a body of this radius at pos overlaps cell (x, y).
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
}

#endif
