#include "engine/gfx/particles.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "engine/core/noise.h"
#include "engine/gfx/mesh.h"

namespace mythos {
namespace {
double Finite(const double v, const double lo, const double hi,
              const double fallback) {
  return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}

Vec3 Add(const Vec3& a, const Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 Scale(const Vec3& a, const double s) {
  return {a.x * s, a.y * s, a.z * s};
}

double Dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 Normalized(const Vec3& a) {
  const double len = std::sqrt(Dot(a, a));
  return len > 1e-9 ? Scale(a, 1.0 / len) : Vec3{0, 1, 0};
}

// A uniformly random direction within `half_angle` of the unit vector `axis`,
// from two random numbers in 0..1.
Vec3 InCone(const Vec3& axis, const double half_angle, const double u1,
            const double u2) {
  const double cos_max = std::cos(half_angle);
  const double cos_t = 1.0 - u1 * (1.0 - cos_max);
  const double sin_t = std::sqrt(std::max(0.0, 1.0 - cos_t * cos_t));
  const double phi = 2.0 * kPi * u2;
  // Any vector not parallel to the axis gives a perpendicular frame.
  const Vec3 helper = std::fabs(axis.y) < 0.9 ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  const Vec3 t = Normalized(Cross(axis, helper));
  const Vec3 b = Cross(axis, t);
  return Add(Scale(axis, cos_t), Add(Scale(t, sin_t * std::cos(phi)),
                                     Scale(b, sin_t * std::sin(phi))));
}

// Moves a particle by `dt`, bouncing off the level. Returns false when it
// should die from a hit.
bool Step(const EmitterParams& p, const Level* const level, const double dt,
          Vec3& pos, Vec3& vel) {
  Vec3 next = {pos.x + vel.x * dt, pos.y + vel.y * dt, pos.z + vel.z * dt};
  if (p.collide) {
    bool hit = false;
    if (level) {
      if (level->PointBlocked(next.x, pos.z)) {
        vel.x = -vel.x * p.bounce;
        next.x = pos.x;
        hit = true;
      }
      if (level->PointBlocked(next.x, next.z)) {
        vel.z = -vel.z * p.bounce;
        next.z = pos.z;
        hit = true;
      }
    }
    const double ceiling = p.floor_y + kWallHeight;
    if (next.y < p.floor_y) {
      next.y = p.floor_y;
      vel.y = -vel.y * p.bounce;
      // What is left of a bounce is a slide, with some friction.
      if (std::fabs(vel.y) < 0.2) vel.y = 0;
      vel.x *= 0.8;
      vel.z *= 0.8;
      hit = true;
    } else if (next.y > ceiling) {
      next.y = ceiling;
      vel.y = -vel.y * p.bounce;
      hit = true;
    }
    if (hit && p.die_on_hit) return false;
  }
  pos = next;
  return true;
}
}  // namespace

EmitterParams Sanitize(const EmitterParams& in) {
  EmitterParams p;
  const int shape = static_cast<int>(in.shape);
  p.shape = shape >= 0 && shape <= 2 ? in.shape : ParticleShape::kSoft;
  const int blend = static_cast<int>(in.blend);
  p.blend = blend >= 0 && blend <= 1 ? in.blend : ParticleBlend::kAlpha;
  p.rate = Finite(in.rate, 0.0, 5000.0, 0.0);
  p.max_particles = std::clamp(in.max_particles, 1, 8192);
  p.life_min = Finite(in.life_min, 0.01, 600.0, 0.5);
  p.life_max = std::max(p.life_min, Finite(in.life_max, 0.01, 600.0, 1.0));
  p.speed_min = Finite(in.speed_min, 0.0, 100.0, 0.5);
  p.speed_max = std::max(p.speed_min, Finite(in.speed_max, 0.0, 100.0, 1.0));
  p.direction = {Finite(in.direction.x, -1e6, 1e6, 0),
                 Finite(in.direction.y, -1e6, 1e6, 1),
                 Finite(in.direction.z, -1e6, 1e6, 0)};
  p.spread = Finite(in.spread, 0.0, kPi, 0.5);
  p.radius = Finite(in.radius, 0.0, 50.0, 0.0);
  p.gravity = {Finite(in.gravity.x, -100, 100, 0),
               Finite(in.gravity.y, -100, 100, 0),
               Finite(in.gravity.z, -100, 100, 0)};
  p.drag = Finite(in.drag, 0.0, 100.0, 0.0);
  p.size_start = Finite(in.size_start, 0.0, 20.0, 0.1);
  p.size_end = Finite(in.size_end, 0.0, 20.0, 0.1);
  p.size_jitter = Finite(in.size_jitter, 0.0, 1.0, 0.0);
  for (int i = 0; i < 4; ++i) {
    p.color_start[i] = static_cast<float>(
        Finite(in.color_start[i], 0.0, 16.0, 1.0));  // above 1 over-drives glow
    p.color_end[i] =
        static_cast<float>(Finite(in.color_end[i], 0.0, 16.0, i == 3 ? 0 : 1));
  }
  p.spin = Finite(in.spin, 0.0, 100.0, 0.0);
  p.collide = in.collide;
  p.bounce = Finite(in.bounce, 0.0, 1.0, 0.3);
  p.die_on_hit = in.die_on_hit;
  p.floor_y = Finite(in.floor_y, -1000.0, 1000.0, 0.0);
  return p;
}

double ParticleSystem::Random() {
  const uint32_t h = Hash32(counter_++ * 0x9e3779b9u ^ seed_);
  return static_cast<double>(h >> 8) / 16777216.0;
}

ParticleSystem::Emitter* ParticleSystem::Find(const int id) {
  for (Emitter& e : emitters_) {
    if (e.id == id && !e.removed) return &e;
  }
  return nullptr;
}

int ParticleSystem::AddEmitter(const EmitterParams& params, const Vec3& pos) {
  if (static_cast<int>(emitters_.size()) >= kMaxEmitters) return 0;
  Emitter e;
  e.id = next_id_++;
  e.params = Sanitize(params);
  e.pos = pos;
  emitters_.push_back(std::move(e));
  return emitters_.back().id;
}

void ParticleSystem::RemoveEmitter(const int id) {
  if (Emitter* e = Find(id)) e->removed = true;
}

void ParticleSystem::SetPosition(const int id, const Vec3& pos) {
  if (Emitter* e = Find(id)) e->pos = pos;
}

void ParticleSystem::SetActive(const int id, const bool active) {
  if (Emitter* e = Find(id)) e->active = active;
}

void ParticleSystem::Burst(const int id, const int count) {
  Emitter* e = Find(id);
  if (!e) return;
  for (int i = 0; i < count; ++i) Spawn(*e);
}

void ParticleSystem::Spawn(Emitter& e) {
  const EmitterParams& p = e.params;
  if (std::cmp_greater_equal(e.particles.size(), p.max_particles)) return;
  Particle q;
  const Vec3 axis = Normalized(p.direction);
  const double u1 = Random(), u2 = Random();
  const Vec3 dir = InCone(axis, p.spread, u1, u2);
  const double speed = Random(p.speed_min, p.speed_max);
  q.vel = Scale(dir, speed);
  q.pos = e.pos;
  if (p.radius > 0) {
    // A random point in the sphere, by rejection with a fixed number of tries.
    for (int i = 0; i < 8; ++i) {
      const Vec3 o = {Random(-1, 1), Random(-1, 1), Random(-1, 1)};
      if (Dot(o, o) <= 1.0) {
        q.pos = Add(e.pos, Scale(o, p.radius));
        break;
      }
    }
  }
  q.life = Random(p.life_min, p.life_max);
  q.size_scale = 1.0 + (Random() * 2.0 - 1.0) * p.size_jitter;
  q.rotation = Random() * 2.0 * kPi;
  q.spin = (Random() * 2.0 - 1.0) * p.spin;
  e.particles.push_back(q);
}

void ParticleSystem::Update(const double dt, const Level* const level) {
  if (!(dt > 0) || !std::isfinite(dt)) return;
  for (Emitter& e : emitters_) {
    const EmitterParams& p = e.params;
    const double drag = std::exp(-p.drag * dt);
    for (size_t i = 0; i < e.particles.size();) {
      Particle& q = e.particles[i];
      q.age += dt;
      bool alive = q.age < q.life;
      if (alive) {
        q.vel = Add(q.vel, Scale(p.gravity, dt));
        q.vel = Scale(q.vel, drag);
        q.rotation += q.spin * dt;
        alive = Step(p, level, dt, q.pos, q.vel);
      }
      if (alive) {
        ++i;
      } else {
        q = e.particles.back();  // order does not matter
        e.particles.pop_back();
      }
    }
    // New particles start after this step, so they are not aged by it.
    if (e.active && !e.removed && p.rate > 0) {
      e.carry += p.rate * dt;
      // A long frame cannot owe more than the emitter can hold.
      e.carry = std::min(e.carry, static_cast<double>(p.max_particles));
      while (e.carry >= 1.0) {
        Spawn(e);
        e.carry -= 1.0;
      }
    }
  }
  std::erase_if(emitters_, [](const Emitter& e) {
    return e.removed && e.particles.empty();
  });
}

void ParticleSystem::Collect(std::vector<ParticleSprite>& out,
                             const int max_sprites) const {
  int room = max_sprites;
  for (const Emitter& e : emitters_) {
    const EmitterParams& p = e.params;
    for (const Particle& q : e.particles) {
      if (room <= 0) return;
      const double t = std::clamp(q.age / q.life, 0.0, 1.0);
      ParticleSprite s;
      s.x = static_cast<float>(q.pos.x);
      s.y = static_cast<float>(q.pos.y);
      s.z = static_cast<float>(q.pos.z);
      s.size = static_cast<float>(
          (p.size_start + (p.size_end - p.size_start) * t) * q.size_scale);
      s.rotation = static_cast<float>(q.rotation);
      for (int i = 0; i < 4; ++i) {
        s.color[i] = static_cast<float>(
            p.color_start[i] + (p.color_end[i] - p.color_start[i]) * t);
      }
      s.shape = p.shape;
      s.blend = p.blend;
      out.push_back(s);
      --room;
    }
  }
}

int ParticleSystem::Count() const {
  int n = 0;
  for (const Emitter& e : emitters_) n += static_cast<int>(e.particles.size());
  return n;
}

void ParticleSystem::Clear() { emitters_.clear(); }
}  // namespace mythos
