#ifndef MYTHOS_ENGINE_GFX_PARTICLES_H_
#define MYTHOS_ENGINE_GFX_PARTICLES_H_

#include <cstdint>
#include <vector>

#include "engine/core/math.h"
#include "engine/world/level.h"

// A CPU particle simulation: dust, sparks, smoke, steam, drips. The game owns a
// `ParticleSystem`, adds emitters and moves them; each frame it updates the
// system and hands the sprites it makes to `CameraState::particles`. Positions
// are world units (x, z on the map, y up). Deterministic from the seed, with
// no clock inside.
namespace mythos {
// How a sprite is shaped (the renderer draws these procedurally).
enum class ParticleShape : int {
  kSoft = 0,   // a round, soft-edged puff of light or dust
  kSpark = 1,  // a small bright core with a short falloff
  kSmoke = 2,  // a ragged, noisy-edged puff
};

// How a sprite is blended into the picture.
enum class ParticleBlend : int {
  kAlpha = 0,     // an ordinary translucent sprite (sorted far to near)
  kAdditive = 1,  // adds its light: sparks, embers, fire, glow
};

// One camera-facing square, ready to draw. The colour is in display space and
// unlit; a game tints it for the light where it stands.
struct ParticleSprite {
  float x = 0, y = 0, z = 0;
  // Edge length, metres.
  float size = 0.1f;
  // In-plane turn, radians.
  float rotation = 0;
  float color[4] = {1, 1, 1, 1};
  ParticleShape shape = ParticleShape::kSoft;
  ParticleBlend blend = ParticleBlend::kAlpha;
};

// Everything that shapes one kind of particle. A value is clamped into a sane
// range when an emitter is added.
struct EmitterParams {
  ParticleShape shape = ParticleShape::kSoft;
  ParticleBlend blend = ParticleBlend::kAlpha;

  // Particles per second while the emitter is active (0 = only bursts).
  double rate = 0;
  // The most live particles of this emitter; spawns beyond it are dropped.
  int max_particles = 256;

  // Life span and launch speed are picked between these at random.
  double life_min = 0.5, life_max = 1.0;
  double speed_min = 0.5, speed_max = 1.0;
  // The launch cone: its axis (need not be a unit vector; zero means up) and
  // half-angle in radians (pi = every direction).
  Vec3 direction = {0, 1, 0};
  double spread = 0.5;
  // Particles start at a random point in a sphere of this radius.
  double radius = 0;

  // Acceleration in m/s^2 (smoke and steam use a negative y, so it rises) and
  // the speed lost to drag, per second.
  Vec3 gravity = {0, 0, 0};
  double drag = 0;

  // Edge length over a life, and how much each particle's size varies (a
  // fraction: 0.3 = plus or minus 30%).
  double size_start = 0.1, size_end = 0.1, size_jitter = 0;
  // Colour (RGBA) over a life, blended linearly.
  float color_start[4] = {1, 1, 1, 1};
  float color_end[4] = {1, 1, 1, 0};
  // Each particle spins at a random rate within plus or minus this, rad/s.
  double spin = 0;

  // Collide with the level's walls, the floor at `floor_y` and the ceiling
  // `kWallHeight` above it (given a level to `Update`). `bounce` is the speed
  // kept after a hit (0 stops it dead, 1 is elastic); `die_on_hit` removes the
  // particle instead.
  bool collide = false;
  double bounce = 0.3;
  bool die_on_hit = false;
  double floor_y = 0;
};

// Returns `p` with every value forced into a safe range (NaN becomes the
// default), so a bad number cannot make a particle explode or never die.
EmitterParams Sanitize(const EmitterParams& p);

// The most sprites one `Collect` returns, and the most emitters (adds beyond it
// fail).
inline constexpr int kMaxEmitters = 64;

class ParticleSystem {
 public:
  explicit ParticleSystem(uint32_t seed = 1) : seed_(seed) {}

  // Adds an emitter at `pos`, active. Returns its id (> 0), or 0 when there
  // are already `kMaxEmitters`.
  int AddEmitter(const EmitterParams& params, const Vec3& pos);

  // Stops an emitter spawning; its live particles fly on and the emitter goes
  // when the last one has died. Unknown ids are ignored.
  void RemoveEmitter(int id);

  // Moves an emitter (live particles are not moved).
  void SetPosition(int id, const Vec3& pos);

  // Starts or stops the continuous `rate` (bursts still work while stopped).
  void SetActive(int id, bool active);

  // Spawns `count` particles at once (clamped by `max_particles`).
  void Burst(int id, int count);

  // Moves everything on by `dt` seconds (negative or NaN is ignored): spawns
  // by rate, ages, moves and collides. `level` may be null (no collision).
  void Update(double dt, const Level* level = nullptr);

  // Appends the live sprites to `out`, at most `max_sprites` in all.
  void Collect(std::vector<ParticleSprite>& out, int max_sprites) const;

  // Live particles in every emitter.
  [[nodiscard]] int Count() const;

  // Live emitters (including ones draining after `RemoveEmitter`).
  [[nodiscard]] int EmitterCount() const {
    return static_cast<int>(emitters_.size());
  }

  // Drops every emitter and particle.
  void Clear();

 private:
  struct Particle {
    Vec3 pos, vel;
    double age = 0, life = 1, size_scale = 1, rotation = 0, spin = 0;
  };

  struct Emitter {
    int id = 0;
    EmitterParams params;
    Vec3 pos;
    bool active = true, removed = false;
    double carry = 0;  // the fraction of a particle owed by `rate`
    std::vector<Particle> particles;
  };

  Emitter* Find(int id);
  void Spawn(Emitter& e);
  double Random();  // 0..1
  double Random(double lo, double hi) { return lo + (hi - lo) * Random(); }

  std::vector<Emitter> emitters_;
  uint32_t seed_;
  uint32_t counter_ = 0;
  int next_id_ = 1;
};
}  // namespace mythos

#endif
