// Skeletal animation on the CPU: a bone hierarchy, keyframed clips, a sampler,
// blending and a crossfade, and the skin matrices a renderer uploads (at most
// kMaxBones, so a uniform array holds them). Headless; no GL.
#ifndef MYTHOS_ENGINE_GFX_SKELETON_H_
#define MYTHOS_ENGINE_GFX_SKELETON_H_

#include <array>
#include <string>
#include <vector>

#include "engine/core/math.h"

namespace mythos {
constexpr int kMaxBones = 64;

struct Quat {
  double x = 0, y = 0, z = 0, w = 1;
};

// Unit length (identity for a zero quaternion).
Quat Normalize(const Quat& q);
// Shortest-arc spherical interpolation, t in 0..1.
Quat Slerp(const Quat& a, const Quat& b, double t);
// Rotation of `radians` about the unit axis.
Quat QuatAxisAngle(const Vec3& axis, double radians);

// Local transform of a bone: scale, then rotate, then translate.
struct BoneTransform {
  Vec3 t;
  Quat r;
  Vec3 s{1, 1, 1};
};

using Mat4f = std::array<float, 16>;  // column-major, like core/mat4.h

Mat4f ToMatrix(const BoneTransform& b);

struct Bone {
  std::string name;
  // Index of the parent, -1 for a root. A parent comes before its children.
  int parent = -1;
  // Local transform in the bind pose.
  BoneTransform bind;
};

class Skeleton {
 public:
  // Fails (false, nothing changed) if there are more than kMaxBones or a
  // parent does not come before its child.
  bool Set(std::vector<Bone> bones);

  [[nodiscard]] const std::vector<Bone>& Bones() const { return bones_; }
  [[nodiscard]] int Count() const { return static_cast<int>(bones_.size()); }
  // Index of the bone called `name`, or -1.
  [[nodiscard]] int Find(const std::string& name) const;
  // Model-to-bone matrices of the bind pose.
  [[nodiscard]] const std::vector<Mat4f>& InverseBind() const {
    return inverse_bind_;
  }

 private:
  std::vector<Bone> bones_;
  std::vector<Mat4f> inverse_bind_;
};

template <typename T>
struct Key {
  double time = 0;
  T value;
};

// The keys of one bone; a channel left empty keeps the bind pose's value.
struct BoneTrack {
  int bone = 0;
  std::vector<Key<Vec3>> translation;
  std::vector<Key<Quat>> rotation;
  std::vector<Key<Vec3>> scale;
};

struct Clip {
  std::string name;
  double duration = 0;
  bool loop = true;
  std::vector<BoneTrack> tracks;
};

// A local transform per bone.
using Pose = std::vector<BoneTransform>;

Pose BindPose(const Skeleton& skeleton);
// The pose at `time` (wrapped for a looping clip, clamped otherwise). Keys
// must be sorted by time; tracks naming a missing bone are ignored.
Pose SamplePose(const Skeleton& skeleton, const Clip& clip, double time);
// a at weight 0, b at weight 1 (poses of equal length; else `a`).
Pose BlendPoses(const Pose& a, const Pose& b, double weight);
// One matrix per bone, model space times the inverse bind: what the vertex
// shader multiplies a skinned vertex by.
std::vector<Mat4f> SkinMatrices(const Skeleton& skeleton, const Pose& pose);

// Plays one clip and fades to the next over a time.
class Animator {
 public:
  explicit Animator(const Skeleton* skeleton) : skeleton_(skeleton) {}

  // Starts `clip` (which must outlive its playing). With fade > 0 the old
  // clip blends out over `fade` seconds; playing the current clip again is
  // ignored.
  void Play(const Clip* clip, double fade = 0.0);
  void Update(double dt);
  [[nodiscard]] Pose CurrentPose() const;
  [[nodiscard]] const Clip* Current() const { return current_; }
  // Whether a non-looping clip has reached its end.
  [[nodiscard]] bool Finished() const;

 private:
  const Skeleton* skeleton_;
  const Clip* current_ = nullptr;
  const Clip* previous_ = nullptr;
  double time_ = 0, previous_time_ = 0;
  double fade_ = 0, fade_left_ = 0;
};
}

#endif
