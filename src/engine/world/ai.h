// A minimal behaviour-tree runtime and perception (sight cone, hearing). The
// engine owns traversal and the senses; leaves are game callbacks, so what an
// agent does is the game's. Headless.
#ifndef MYTHOS_ENGINE_WORLD_AI_H_
#define MYTHOS_ENGINE_WORLD_AI_H_

#include <functional>
#include <memory>
#include <vector>

#include "engine/core/math.h"
#include "engine/world/level.h"

namespace mythos::ai {
enum class Status { kSuccess, kFailure, kRunning };

// A node of the tree. Composites are reactive: they re-evaluate from their
// first child every tick, so a condition early in a sequence can interrupt
// what follows it.
class Node {
 public:
  virtual ~Node() = default;
  virtual Status Tick(double dt) = 0;
};
using NodePtr = std::unique_ptr<Node>;

// A game-supplied leaf (an action or a condition).
NodePtr Leaf(std::function<Status(double dt)> fn);
// A leaf that is kSuccess when `test` is true and kFailure otherwise.
NodePtr Condition(std::function<bool()> test);
// Runs children in order; stops at the first one that is not kSuccess.
NodePtr Sequence(std::vector<NodePtr> children);
// Runs children in order; stops at the first one that is not kFailure.
NodePtr Selector(std::vector<NodePtr> children);
// Swaps success and failure; running stays running.
NodePtr Inverter(NodePtr child);
// Turns a failure into a success.
NodePtr Succeeder(NodePtr child);
// After the child finishes (not running), fails for `seconds` without
// running it.
NodePtr Cooldown(double seconds, NodePtr child);

// An agent's sight: a cone of `fov` radians (full width) and `range`, facing
// `angle` (the player's convention: direction (cos, sin) on the (x, z)
// plane, Vec2::y being z).
struct Sight {
  Vec2 eye;
  double angle = 0;
  double fov = 2.0;
  double range = 8.0;
};

// True if `target` is inside the cone with a clear line over the level.
bool CanSee(const Level& level, const Sight& sight, const Vec2& target);

// A sound at a place; it carries `loudness` metres in the open.
struct Noise {
  Vec2 pos;
  double loudness = 0;
};

// True if a listener at `ear` with the given sensitivity (a multiplier of
// the loudness, 1 normal) hears `noise`. Without a clear line the distance
// counts double: walls muffle.
bool CanHear(const Level& level, const Vec2& ear, double sensitivity,
             const Noise& noise);
}

#endif
