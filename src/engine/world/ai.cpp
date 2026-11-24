#include "engine/world/ai.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace mythos::ai {
namespace {
class LeafNode : public Node {
 public:
  explicit LeafNode(std::function<Status(double)> fn) : fn_(std::move(fn)) {}
  Status Tick(double dt) override { return fn_ ? fn_(dt) : Status::kFailure; }

 private:
  std::function<Status(double)> fn_;
};

// Stops at the first child whose status is not `pass`.
class Composite : public Node {
 public:
  Composite(std::vector<NodePtr> children, Status pass)
      : children_(std::move(children)), pass_(pass) {}
  Status Tick(double dt) override {
    for (auto& c : children_) {
      const Status s = c->Tick(dt);
      if (s != pass_) return s;
    }
    return pass_;
  }

 private:
  std::vector<NodePtr> children_;
  Status pass_;
};

class Inverted : public Node {
 public:
  explicit Inverted(NodePtr child) : child_(std::move(child)) {}
  Status Tick(double dt) override {
    const Status s = child_->Tick(dt);
    if (s == Status::kRunning) return s;
    return s == Status::kSuccess ? Status::kFailure : Status::kSuccess;
  }

 private:
  NodePtr child_;
};

class Succeeds : public Node {
 public:
  explicit Succeeds(NodePtr child) : child_(std::move(child)) {}
  Status Tick(double dt) override {
    const Status s = child_->Tick(dt);
    return s == Status::kRunning ? s : Status::kSuccess;
  }

 private:
  NodePtr child_;
};

class CooldownNode : public Node {
 public:
  CooldownNode(double seconds, NodePtr child)
      : seconds_(seconds), child_(std::move(child)) {}
  Status Tick(double dt) override {
    if (left_ > 0) {
      left_ -= dt;
      return Status::kFailure;
    }
    const Status s = child_->Tick(dt);
    if (s != Status::kRunning) left_ = seconds_;
    return s;
  }

 private:
  double seconds_;
  double left_ = 0;
  NodePtr child_;
};
}

NodePtr Leaf(std::function<Status(double)> fn) {
  return std::make_unique<LeafNode>(std::move(fn));
}

NodePtr Condition(std::function<bool()> test) {
  return Leaf([test = std::move(test)](double) {
    return test && test() ? Status::kSuccess : Status::kFailure;
  });
}

NodePtr Sequence(std::vector<NodePtr> children) {
  return std::make_unique<Composite>(std::move(children), Status::kSuccess);
}

NodePtr Selector(std::vector<NodePtr> children) {
  return std::make_unique<Composite>(std::move(children), Status::kFailure);
}

NodePtr Inverter(NodePtr child) {
  return std::make_unique<Inverted>(std::move(child));
}

NodePtr Succeeder(NodePtr child) {
  return std::make_unique<Succeeds>(std::move(child));
}

NodePtr Cooldown(double seconds, NodePtr child) {
  return std::make_unique<CooldownNode>(seconds, std::move(child));
}

bool CanSee(const Level& level, const Sight& sight, const Vec2& target) {
  const double dx = target.x - sight.eye.x;
  const double dy = target.y - sight.eye.y;
  const double dist = std::hypot(dx, dy);
  if (dist > sight.range) return false;
  // Standing on the spot is seen.
  if (dist > 1e-9) {
    double diff = std::atan2(dy, dx) - sight.angle;
    constexpr double kPi = std::numbers::pi;
    diff = std::remainder(diff, 2 * kPi);
    if (std::fabs(diff) > sight.fov / 2) return false;
  }
  return level.LineOfSight(sight.eye.x, sight.eye.y, target.x, target.y);
}

void NoiseBoard::Add(const Noise& noise, const double now) {
  // Drop what has been forgotten, so the list stays short.
  std::erase_if(noises_,
                [&](const Entry& e) { return now - e.time > lifetime_; });
  noises_.push_back({noise, now});
}

std::optional<Noise> NoiseBoard::Loudest(const Level& level, const Vec2& ear,
                                         const double sensitivity,
                                         const double now) const {
  std::optional<Noise> best;
  double best_left = -1e30;
  for (const Entry& e : noises_) {
    if (now - e.time > lifetime_ ||
        !CanHear(level, ear, sensitivity, e.noise)) {
      continue;
    }
    double dist = std::hypot(e.noise.pos.x - ear.x, e.noise.pos.y - ear.y);
    if (!level.LineOfSight(e.noise.pos.x, e.noise.pos.y, ear.x, ear.y)) {
      dist *= 2;
    }
    const double left = e.noise.loudness * sensitivity - dist;
    if (left > best_left) {
      best_left = left;
      best = e.noise;
    }
  }
  return best;
}

double LastSeen::Age(const double now) const {
  return known_ ? now - time_ : std::numeric_limits<double>::infinity();
}

bool CanHear(const Level& level, const Vec2& ear, const double sensitivity,
             const Noise& noise) {
  double dist = std::hypot(noise.pos.x - ear.x, noise.pos.y - ear.y);
  if (!level.LineOfSight(noise.pos.x, noise.pos.y, ear.x, ear.y)) dist *= 2;
  return dist <= noise.loudness * std::max(0.0, sensitivity);
}
}
