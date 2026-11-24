#include "engine/world/cutscene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <locale>
#include <sstream>
#include <utility>

namespace mythos {
namespace {
constexpr std::array<const char*, kEaseCount> kEaseNames = {
    "linear",      "smoothstep", "in_quad",     "out_quad",
    "in_out_quad", "in_cubic",   "out_cubic",   "in_out_cubic",
    "in_out_sine", "out_back",   "out_elastic", "out_bounce"};

std::string Trim(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

std::string Num(const double v) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out.precision(9);
  out << v;
  return out.str();
}

template <typename T>
void SortByTime(std::vector<T>& keys) {
  std::ranges::stable_sort(
      keys, [](const T& a, const T& b) { return a.time < b.time; });
}

double Lerp(const double a, const double b, const double t) {
  return a + (b - a) * t;
}

// The two keys around `time` and the eased blend between them.
template <typename T>
void Bracket(const std::vector<T>& keys, const double time, const T** a,
             const T** b, double* t) {
  if (time <= keys.front().time || keys.size() == 1) {
    *a = *b = &keys.front();
    *t = 0;
    return;
  }
  if (time >= keys.back().time) {
    *a = *b = &keys.back();
    *t = 0;
    return;
  }
  size_t i = 1;
  while (keys[i].time < time) ++i;
  *a = &keys[i - 1];
  *b = &keys[i];
  const double span = keys[i].time - keys[i - 1].time;
  *t = span > 1e-12 ? ApplyEase(keys[i].ease, (time - keys[i - 1].time) / span)
                    : 1.0;
}
}

double CutsceneLength(const Cutscene& scene) {
  double length = scene.duration;
  for (const auto& k : scene.camera) length = std::max(length, k.time);
  for (const auto& k : scene.cues) length = std::max(length, k.time);
  for (const auto& k : scene.anims) length = std::max(length, k.time);
  for (const auto& track : scene.values) {
    for (const auto& k : track.keys) length = std::max(length, k.time);
  }
  return length;
}

std::string EaseName(const Ease ease) {
  const auto i = static_cast<size_t>(ease);
  return i < kEaseNames.size() ? kEaseNames[i] : "linear";
}

bool ParseEase(const std::string& name, Ease* const out) {
  for (size_t i = 0; i < kEaseNames.size(); ++i) {
    if (name == kEaseNames[i]) {
      *out = static_cast<Ease>(i);
      return true;
    }
  }
  return false;
}

void SortCutscene(Cutscene& scene) {
  SortByTime(scene.camera);
  SortByTime(scene.cues);
  SortByTime(scene.anims);
  for (auto& track : scene.values) SortByTime(track.keys);
}

std::vector<std::string> ValidateCutscene(const Cutscene& scene) {
  std::vector<std::string> problems;
  const auto bad_time = [](const double t) {
    return !std::isfinite(t) || t < 0;
  };
  if (bad_time(scene.duration))
    problems.emplace_back("the duration is not a time");
  for (size_t i = 0; i < scene.camera.size(); ++i) {
    const CameraKey& k = scene.camera[i];
    if (bad_time(k.time)) {
      problems.push_back("camera key " + std::to_string(i + 1) + ": bad time");
    }
    for (const double v :
         {k.pos.x, k.pos.y, k.pos.z, k.yaw, k.pitch, k.fov_scale}) {
      if (!std::isfinite(v)) {
        problems.push_back("camera key " + std::to_string(i + 1) +
                           ": a number is not finite");
        break;
      }
    }
  }
  for (size_t i = 0; i < scene.cues.size(); ++i) {
    if (bad_time(scene.cues[i].time)) {
      problems.push_back("cue " + std::to_string(i + 1) + ": bad time");
    }
    if (scene.cues[i].action.type.empty()) {
      problems.push_back("cue " + std::to_string(i + 1) + ": no type");
    }
  }
  for (size_t i = 0; i < scene.anims.size(); ++i) {
    const AnimKey& k = scene.anims[i];
    if (bad_time(k.time)) {
      problems.push_back("animation key " + std::to_string(i + 1) +
                         ": bad time");
    }
    if (k.actor.empty() || k.clip.empty()) {
      problems.push_back("animation key " + std::to_string(i + 1) +
                         ": needs an actor and a clip");
    }
  }
  for (size_t i = 0; i < scene.values.size(); ++i) {
    const ValueTrack& t = scene.values[i];
    if (t.name.empty()) {
      problems.push_back("value track " + std::to_string(i + 1) + ": no name");
    }
    for (size_t j = 0; j < i; ++j) {
      if (!t.name.empty() && scene.values[j].name == t.name) {
        problems.push_back("value track '" + t.name + "' appears twice");
        break;
      }
    }
    for (const ValueKey& k : t.keys) {
      if (bad_time(k.time) || !std::isfinite(k.value)) {
        problems.push_back("value track '" + t.name + "': a bad key");
        break;
      }
    }
  }
  if (scene.camera.empty() && scene.cues.empty() && scene.anims.empty() &&
      scene.values.empty() && scene.duration <= 0) {
    problems.emplace_back("nothing to play: no keys and no duration");
  }
  return problems;
}

bool ParseCutscene(const std::string& text, Cutscene* const out,
                   std::string* const error) {
  const auto fail = [&](const std::string& why) {
    if (error != nullptr) *error = why;
    return false;
  };
  Cutscene scene;
  std::string section;
  std::istringstream in(text);
  in.imbue(std::locale::classic());
  for (std::string raw; std::getline(in, raw);) {
    const std::string line = Trim(raw);
    if (line.empty() || line[0] == ';') continue;
    if (line[0] == '[') {
      section = line;
      if (section != "[camera]" && section != "[cues]" &&
          section != "[values]" && section != "[anims]") {
        return fail("unknown section: " + line);
      }
      continue;
    }
    std::istringstream ls(line);
    ls.imbue(std::locale::classic());
    if (section.empty()) {
      std::string word;
      if (!(ls >> word >> scene.duration) || word != "duration" ||
          scene.duration < 0) {
        return fail("bad line: " + line);
      }
    } else if (section == "[camera]") {
      CameraKey k;
      std::string ease;
      if (!(ls >> k.time >> k.pos.x >> k.pos.y >> k.pos.z >> k.yaw >> k.pitch >>
            k.fov_scale >> ease) ||
          !ParseEase(ease, &k.ease) || k.time < 0) {
        return fail("bad camera line: " + line);
      }
      scene.camera.push_back(k);
    } else if (section == "[cues]") {
      CueKey k;
      if (!(ls >> k.time >> k.action.type) || k.time < 0) {
        return fail("bad cue line: " + line);
      }
      std::string rest;
      std::getline(ls, rest);
      k.action.argument = Trim(rest);
      scene.cues.push_back(std::move(k));
    } else if (section == "[values]") {
      std::string name, ease;
      ValueKey k;
      if (!(ls >> name >> k.time >> k.value >> ease) ||
          !ParseEase(ease, &k.ease) || k.time < 0) {
        return fail("bad value line: " + line);
      }
      auto it = std::ranges::find_if(
          scene.values, [&](const ValueTrack& t) { return t.name == name; });
      if (it == scene.values.end()) {
        scene.values.push_back({name, {}});
        it = scene.values.end() - 1;
      }
      it->keys.push_back(k);
    } else {
      AnimKey k;
      if (!(ls >> k.time >> k.actor >> k.clip >> k.fade) || k.time < 0) {
        return fail("bad anim line: " + line);
      }
      scene.anims.push_back(std::move(k));
    }
  }
  SortByTime(scene.camera);
  SortByTime(scene.cues);
  SortByTime(scene.anims);
  for (auto& track : scene.values) SortByTime(track.keys);
  *out = std::move(scene);
  return true;
}

std::string SerializeCutscene(const Cutscene& scene) {
  std::string s = "; A cutscene: see engine/world/cutscene.h.\n";
  if (scene.duration > 0) s += "duration " + Num(scene.duration) + "\n";
  s += "[camera]\n";
  for (const auto& k : scene.camera) {
    s += Num(k.time) + " " + Num(k.pos.x) + " " + Num(k.pos.y) + " " +
         Num(k.pos.z) + " " + Num(k.yaw) + " " + Num(k.pitch) + " " +
         Num(k.fov_scale) + " " + EaseName(k.ease) + "\n";
  }
  s += "[cues]\n";
  for (const auto& k : scene.cues) {
    s += Num(k.time) + " " + k.action.type;
    if (!k.action.argument.empty()) s += " " + k.action.argument;
    s += '\n';
  }
  s += "[values]\n";
  for (const auto& track : scene.values) {
    for (const auto& k : track.keys) {
      s += track.name + " " + Num(k.time) + " " + Num(k.value) + " " +
           EaseName(k.ease) + "\n";
    }
  }
  s += "[anims]\n";
  for (const auto& k : scene.anims) {
    s += Num(k.time) + " " + k.actor + " " + k.clip + " " + Num(k.fade) + "\n";
  }
  return s;
}

bool SampleCutsceneCamera(const Cutscene& scene, const double t,
                          CameraSample* const out) {
  Cutscene sorted = scene;
  SortCutscene(sorted);
  CutscenePlayer player;
  player.Start(&sorted);
  player.Update(std::max(0.0, t), {}, {});
  return player.Camera(out);
}

double SampleCutsceneValue(const Cutscene& scene, const std::string& name,
                           const double t, const double fallback) {
  Cutscene sorted = scene;
  SortCutscene(sorted);
  CutscenePlayer player;
  player.Start(&sorted);
  player.Update(std::max(0.0, t), {}, {});
  return player.Value(name, fallback);
}

void CutscenePlayer::Start(const Cutscene* const scene) {
  scene_ = scene;
  time_ = 0;
  next_cue_ = next_anim_ = 0;
  done_ = false;
  started_ = false;
  cues_.clear();
  anims_.clear();
  if (scene_ == nullptr) return;
  cues_ = scene_->cues;
  anims_ = scene_->anims;
  SortByTime(cues_);
  SortByTime(anims_);
  length_ = CutsceneLength(*scene_);
}

void CutscenePlayer::Update(const double dt, const CueCallback& on_cue,
                            const AnimCallback& on_anim) {
  if (!Playing()) return;
  // The first update begins at 0, so a key at 0 fires even with a zero step.
  time_ = started_ ? time_ + std::max(0.0, dt) : std::max(0.0, dt);
  started_ = true;
  while (next_cue_ < cues_.size() && cues_[next_cue_].time <= time_) {
    if (on_cue) on_cue(cues_[next_cue_].action);
    ++next_cue_;
  }
  while (next_anim_ < anims_.size() && anims_[next_anim_].time <= time_) {
    if (on_anim) on_anim(anims_[next_anim_]);
    ++next_anim_;
  }
  if (time_ >= length_) {
    time_ = length_;
    done_ = true;
  }
}

void CutscenePlayer::Skip(const CueCallback& on_cue,
                          const AnimCallback& on_anim) {
  if (!Playing()) return;
  started_ = true;
  for (; next_cue_ < cues_.size(); ++next_cue_) {
    if (on_cue) on_cue(cues_[next_cue_].action);
  }
  for (; next_anim_ < anims_.size(); ++next_anim_) {
    if (on_anim) on_anim(anims_[next_anim_]);
  }
  time_ = length_;
  done_ = true;
}

bool CutscenePlayer::Camera(CameraSample* const out) const {
  if (scene_ == nullptr || scene_->camera.empty()) return false;
  const CameraKey *a = nullptr, *b = nullptr;
  double t = 0;
  Bracket(scene_->camera, time_, &a, &b, &t);
  out->pos = {Lerp(a->pos.x, b->pos.x, t), Lerp(a->pos.y, b->pos.y, t),
              Lerp(a->pos.z, b->pos.z, t)};
  // Turn the short way round.
  out->yaw = a->yaw + AngleDiff(b->yaw, a->yaw) * t;
  out->pitch = Lerp(a->pitch, b->pitch, t);
  out->fov_scale = Lerp(a->fov_scale, b->fov_scale, t);
  return true;
}

double CutscenePlayer::Value(const std::string& name,
                             const double fallback) const {
  if (scene_ == nullptr) return fallback;
  for (const ValueTrack& track : scene_->values) {
    if (track.name != name || track.keys.empty()) continue;
    const ValueKey *a = nullptr, *b = nullptr;
    double t = 0;
    Bracket(track.keys, time_, &a, &b, &t);
    return Lerp(a->value, b->value, t);
  }
  return fallback;
}
}
