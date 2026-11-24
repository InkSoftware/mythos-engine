#include "engine/gfx/skeleton.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "engine/core/mat4.h"

namespace mythos {
Quat Normalize(const Quat& q) {
  const double n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (n < 1e-12) return {};
  return {q.x / n, q.y / n, q.z / n, q.w / n};
}

Quat Slerp(const Quat& a, const Quat& b_in, const double t) {
  Quat b = b_in;
  double dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  if (dot < 0) {
    dot = -dot;
    b = {-b.x, -b.y, -b.z, -b.w};
  }
  double wa = 1 - t, wb = t;
  if (dot < 0.9995) {
    const double theta = std::acos(std::min(1.0, dot));
    const double s = std::sin(theta);
    wa = std::sin((1 - t) * theta) / s;
    wb = std::sin(t * theta) / s;
  }
  return Normalize({wa * a.x + wb * b.x, wa * a.y + wb * b.y,
                    wa * a.z + wb * b.z, wa * a.w + wb * b.w});
}

Quat QuatAxisAngle(const Vec3& axis, const double radians) {
  const double n =
      std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
  if (n < 1e-12) return {};
  const double s = std::sin(radians / 2) / n;
  return {axis.x * s, axis.y * s, axis.z * s, std::cos(radians / 2)};
}

Mat4f ToMatrix(const BoneTransform& b) {
  const Quat q = Normalize(b.r);
  const double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
  const double xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
  const double wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
  Mat4f m{};
  // Columns are the scaled rotated axes.
  m[0] = static_cast<float>((1 - 2 * (yy + zz)) * b.s.x);
  m[1] = static_cast<float>(2 * (xy + wz) * b.s.x);
  m[2] = static_cast<float>(2 * (xz - wy) * b.s.x);
  m[4] = static_cast<float>(2 * (xy - wz) * b.s.y);
  m[5] = static_cast<float>((1 - 2 * (xx + zz)) * b.s.y);
  m[6] = static_cast<float>(2 * (yz + wx) * b.s.y);
  m[8] = static_cast<float>(2 * (xz + wy) * b.s.z);
  m[9] = static_cast<float>(2 * (yz - wx) * b.s.z);
  m[10] = static_cast<float>((1 - 2 * (xx + yy)) * b.s.z);
  m[12] = static_cast<float>(b.t.x);
  m[13] = static_cast<float>(b.t.y);
  m[14] = static_cast<float>(b.t.z);
  m[15] = 1.0f;
  return m;
}

namespace {
Mat4f Multiply(const Mat4f& a, const Mat4f& b) {
  Mat4f out;
  Mat4Multiply(a.data(), b.data(), out.data());
  return out;
}

// Inverse of a rigid-plus-scale matrix from ToMatrix (rotation, per-axis
// scale, translation).
Mat4f InvertTrs(const Mat4f& m) {
  Mat4f r{};
  // Each column of the 3x3 part has length s; the inverse is R^T / s.
  for (int c = 0; c < 3; ++c) {
    const float len2 = m[c * 4] * m[c * 4] + m[c * 4 + 1] * m[c * 4 + 1] +
                       m[c * 4 + 2] * m[c * 4 + 2];
    const float inv = len2 > 1e-20f ? 1.0f / len2 : 0.0f;
    for (int row = 0; row < 3; ++row) r[row * 4 + c] = m[c * 4 + row] * inv;
  }
  for (int row = 0; row < 3; ++row) {
    r[12 + row] = -(r[row] * m[12] + r[4 + row] * m[13] + r[8 + row] * m[14]);
  }
  r[15] = 1.0f;
  return r;
}

std::vector<Mat4f> Globals(const Skeleton& skeleton, const Pose& pose) {
  std::vector<Mat4f> g(pose.size());
  for (size_t i = 0; i < pose.size(); ++i) {
    const Mat4f local = ToMatrix(pose[i]);
    const int parent = skeleton.Bones()[i].parent;
    g[i] = parent < 0 ? local : Multiply(g[static_cast<size_t>(parent)], local);
  }
  return g;
}

// Finds the keys around `time` and the blend between them.
template <typename T>
bool Bracket(const std::vector<Key<T>>& keys, const double time, size_t& i0,
             size_t& i1, double& t) {
  if (keys.empty()) return false;
  if (time <= keys.front().time || keys.size() == 1) {
    i0 = i1 = 0;
    t = 0;
    return true;
  }
  if (time >= keys.back().time) {
    i0 = i1 = keys.size() - 1;
    t = 0;
    return true;
  }
  i1 = static_cast<size_t>(
      std::upper_bound(keys.begin(), keys.end(), time,
                       [](double v, const Key<T>& k) { return v < k.time; }) -
      keys.begin());
  i0 = i1 - 1;
  const double span = keys[i1].time - keys[i0].time;
  t = span > 1e-12 ? (time - keys[i0].time) / span : 0;
  return true;
}

Vec3 Lerp(const Vec3& a, const Vec3& b, const double t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

double ClipTime(const Clip& clip, const double time) {
  if (clip.duration <= 0) return 0;
  if (clip.loop) {
    const double w = std::fmod(time, clip.duration);
    return w < 0 ? w + clip.duration : w;
  }
  return std::clamp(time, 0.0, clip.duration);
}
}

bool Skeleton::Set(std::vector<Bone> bones) {
  if (bones.size() > static_cast<size_t>(kMaxBones)) return false;
  for (size_t i = 0; i < bones.size(); ++i) {
    if (bones[i].parent >= static_cast<int>(i) || bones[i].parent < -1) {
      return false;
    }
  }
  bones_ = std::move(bones);
  Pose bind = BindPose(*this);
  inverse_bind_.clear();
  for (const Mat4f& g : Globals(*this, bind)) {
    inverse_bind_.push_back(InvertTrs(g));
  }
  return true;
}

int Skeleton::Find(const std::string& name) const {
  for (size_t i = 0; i < bones_.size(); ++i) {
    if (bones_[i].name == name) return static_cast<int>(i);
  }
  return -1;
}

Pose BindPose(const Skeleton& skeleton) {
  Pose pose;
  for (const Bone& b : skeleton.Bones()) pose.push_back(b.bind);
  return pose;
}

Pose SamplePose(const Skeleton& skeleton, const Clip& clip, const double time) {
  Pose pose = BindPose(skeleton);
  const double at = ClipTime(clip, time);
  for (const BoneTrack& track : clip.tracks) {
    if (track.bone < 0 || track.bone >= skeleton.Count()) continue;
    BoneTransform& out = pose[static_cast<size_t>(track.bone)];
    size_t i0, i1;
    double t;
    if (Bracket(track.translation, at, i0, i1, t)) {
      out.t = Lerp(track.translation[i0].value, track.translation[i1].value, t);
    }
    if (Bracket(track.rotation, at, i0, i1, t)) {
      out.r = Slerp(track.rotation[i0].value, track.rotation[i1].value, t);
    }
    if (Bracket(track.scale, at, i0, i1, t)) {
      out.s = Lerp(track.scale[i0].value, track.scale[i1].value, t);
    }
  }
  return pose;
}

Pose BlendPoses(const Pose& a, const Pose& b, double weight) {
  if (a.size() != b.size()) return a;
  weight = std::clamp(weight, 0.0, 1.0);
  Pose out(a.size());
  for (size_t i = 0; i < a.size(); ++i) {
    out[i].t = Lerp(a[i].t, b[i].t, weight);
    out[i].r = Slerp(a[i].r, b[i].r, weight);
    out[i].s = Lerp(a[i].s, b[i].s, weight);
  }
  return out;
}

std::vector<Mat4f> SkinMatrices(const Skeleton& skeleton, const Pose& pose) {
  std::vector<Mat4f> out;
  if (pose.size() != static_cast<size_t>(skeleton.Count())) return out;
  const std::vector<Mat4f> globals = Globals(skeleton, pose);
  for (size_t i = 0; i < globals.size(); ++i) {
    out.push_back(Multiply(globals[i], skeleton.InverseBind()[i]));
  }
  return out;
}

void Animator::Play(const Clip* clip, const double fade) {
  if (clip == current_) return;
  if (fade > 0 && current_ != nullptr) {
    previous_ = current_;
    previous_time_ = time_;
    fade_ = fade_left_ = fade;
  } else {
    previous_ = nullptr;
    fade_left_ = 0;
  }
  current_ = clip;
  time_ = 0;
}

void Animator::Update(const double dt) {
  if (current_ == nullptr) return;
  time_ += dt;
  if (previous_ != nullptr) {
    previous_time_ += dt;
    fade_left_ -= dt;
    if (fade_left_ <= 0) previous_ = nullptr;
  }
}

Pose Animator::CurrentPose() const {
  if (current_ == nullptr) return BindPose(*skeleton_);
  Pose now = SamplePose(*skeleton_, *current_, time_);
  if (previous_ == nullptr) return now;
  return BlendPoses(SamplePose(*skeleton_, *previous_, previous_time_), now,
                    1.0 - fade_left_ / fade_);
}

bool Animator::Finished() const {
  return current_ != nullptr && !current_->loop && time_ >= current_->duration;
}
}
