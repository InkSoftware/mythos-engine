#include "engine/platform/audio.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <numbers>
#include <string>

#include "engine/audio/spatial.h"
#include "engine/core/vfs.h"

namespace mythos {
namespace {
// Further voices are dropped rather than allocating on the audio thread.
constexpr size_t kMaxVoices = 24;
constexpr float kMinPitch = 0.25f;
constexpr float kVoiceFadeOutSeconds = 0.15f;
constexpr float kAmbienceCrossfadeSeconds = 0.8f;
constexpr float kDuckEaseSeconds = 0.3f;
constexpr float kGainEaseSeconds = 0.06f;
// A quiet spatial voice ducks the ambience less: full ducking at 0.5 gain.
constexpr float kSpatialDuckPerGain = 2.0f;
// Non-spatial voices pan at 1/sqrt(2) each side (equal power); this restores
// unity gain.
constexpr float kCentrePanCompensation = std::numbers::sqrt2_v<float>;

// Frames mixed per pass of the audio callback.
constexpr int kMixChunkFrames = 512;
constexpr int kChannels = 2;

// Per-sample increment that covers 0..1 in `seconds`.
float PerSample(const float seconds, const int rate) {
  return 1.0f / (seconds * static_cast<float>(rate));
}

// Moves `current` toward `target` by at most `max_step`.
float EaseToward(const float current, const float target,
                 const float max_step) {
  return current + std::max(-max_step, std::min(max_step, target - current));
}
}

bool AudioEngine::Init() {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
  SDL_AudioSpec want{};
  want.freq = rate_;
  want.format = SDL_AUDIO_F32;
  want.channels = kChannels;
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want,
                                      &AudioEngine::Callback, this);
  if (!stream_) return false;
  SDL_AudioSpec have{};
  int frames = 0;
  if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream_), &have,
                               &frames) &&
      have.freq > 0) {
    rate_ = have.freq;
  }
  reverb_.Prepare(static_cast<double>(rate_));
  SDL_ResumeAudioStreamDevice(stream_);
  return true;
}

void AudioEngine::Shutdown() {
  if (stream_) {
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
  }
}

int AudioEngine::LoadDir(const std::string& dir,
                         const std::vector<std::string>& names) {
  int loaded = 0;
  for (const auto& name : names) {
    std::string path = dir;
    path += '/';
    path += name;
    path += ".wav";
    SDL_AudioSpec spec{};
    Uint8* data = nullptr;
    Uint32 len = 0;
    std::string file;
    if (!vfs::ReadFile(path, file)) continue;
    SDL_IOStream* io = SDL_IOFromConstMem(file.data(), file.size());
    if (!io || !SDL_LoadWAV_IO(io, true, &spec, &data, &len)) continue;

    SDL_AudioSpec target{};
    target.format = SDL_AUDIO_F32;
    target.channels = 1;
    target.freq = rate_;
    Uint8* out = nullptr;
    int out_len = 0;
    const bool ok = SDL_ConvertAudioSamples(&spec, data, static_cast<int>(len),
                                            &target, &out, &out_len);
    SDL_free(data);
    if (!ok) continue;
    Clip clip;
    const auto* converted = reinterpret_cast<const float*>(out);
    const size_t count = static_cast<size_t>(out_len) / sizeof(float);
    clip.samples.assign(converted, converted + count);
    SDL_free(out);
    if (clip.samples.empty()) continue;
    SDL_LockAudioStream(stream_);
    clips_[name] = std::move(clip);
    SDL_UnlockAudioStream(stream_);
    ++loaded;
  }
  return loaded;
}

void AudioEngine::Play(const std::string& name, const float gain,
                       const float pitch, const std::string& group) {
  if (!stream_) return;
  const Clip* clip = FindClip(name);
  if (!clip) return;
  Voice v;
  v.clip = clip;
  v.step = std::max(kMinPitch, pitch);
  v.gain = gain * effect_scale_;
  v.group = group;
  StartVoice(v);
}

void AudioEngine::PlayVoice(const std::string& name, const float gain,
                            const bool spatial, const bool radio) {
  if (!stream_) return;
  StopVoice();
  const Clip* clip = FindClip(name);
  if (!clip) return;
  Voice v;
  v.clip = clip;
  v.gain = gain * effect_scale_;
  v.voice = true;
  v.group = "voice";
  v.spatial = spatial;
  if (radio) {
    v.radio = std::make_shared<RadioFm>(rate_, 0x9e3779b9u * ++radio_lines_);
  }
  StartVoice(v);
}

void AudioEngine::StartVoice(const Voice& voice) {
  SDL_LockAudioStream(stream_);
  if (voices_.size() < kMaxVoices) voices_.push_back(voice);
  SDL_UnlockAudioStream(stream_);
}

const AudioEngine::Clip* AudioEngine::FindClip(const std::string& name) const {
  const auto it = clips_.find(name);
  return it == clips_.end() ? nullptr : &it->second;
}

void AudioEngine::StopVoice() {
  if (!stream_) return;
  const float fade_per_sample = PerSample(kVoiceFadeOutSeconds, rate_);
  SDL_LockAudioStream(stream_);
  for (auto& v : voices_) {
    if (v.voice) v.fade_rate = -fade_per_sample;
  }
  SDL_UnlockAudioStream(stream_);
}

void AudioEngine::SetVoicePlacement(const float left, const float right) {
  if (!stream_) return;
  SDL_LockAudioStream(stream_);
  for (auto& v : voices_) {
    if (!v.voice || !v.spatial || v.fade_rate < 0.0f) continue;
    v.tgt_l = std::max(0.0f, left);
    v.tgt_r = std::max(0.0f, right);
    if (!v.placed) {
      v.placed = true;
      v.cur_l = v.tgt_l;
      v.cur_r = v.tgt_r;
    }
  }
  SDL_UnlockAudioStream(stream_);
}

void AudioEngine::SetPaused(const bool paused) {
  if (!stream_) return;
  SDL_LockAudioStream(stream_);
  paused_ = paused;
  SDL_UnlockAudioStream(stream_);
}

void AudioEngine::SetVoiceDuck(const float duck) {
  if (!stream_) return;
  SDL_LockAudioStream(stream_);
  voice_duck_ = std::max(0.0f, std::min(1.0f, duck));
  SDL_UnlockAudioStream(stream_);
}

double AudioEngine::ClipSeconds(const std::string& name) const {
  const auto it = clips_.find(name);
  if (it == clips_.end() || rate_ <= 0) return 0.0;
  return static_cast<double>(it->second.samples.size()) / rate_;
}

void AudioEngine::SetVolumes(const float master, const float ambience,
                             const float effects) {
  if (!stream_) return;
  SDL_LockAudioStream(stream_);
  effect_scale_ = master * effects;
  loop_scale_ = master * ambience;
  SDL_UnlockAudioStream(stream_);
}

int AudioEngine::AddSource(const std::string& name, const std::string& group) {
  if (!stream_) return -1;
  const Clip* clip = FindClip(name);
  if (!clip || clip->samples.empty()) return -1;
  Source s;
  s.clip = clip;
  s.group = group;
  SDL_LockAudioStream(stream_);
  sources_.push_back(s);
  const int id = static_cast<int>(sources_.size()) - 1;
  SDL_UnlockAudioStream(stream_);
  return id;
}

void AudioEngine::SetSource(const int id, const float left, const float right) {
  if (!stream_ || id < 0) return;
  SDL_LockAudioStream(stream_);
  if (static_cast<size_t>(id) < sources_.size()) {
    sources_[static_cast<size_t>(id)].target_l = std::max(0.0f, left);
    sources_[static_cast<size_t>(id)].target_r = std::max(0.0f, right);
  }
  SDL_UnlockAudioStream(stream_);
}

void AudioEngine::SetGroupVolume(const std::string& group, const float volume) {
  if (!stream_) return;
  SDL_LockAudioStream(stream_);
  groups_.SetVolume(group, static_cast<double>(volume));
  SDL_UnlockAudioStream(stream_);
}

void AudioEngine::AddGroupDuck(const std::string& group, const std::string& by,
                               const float amount, const float attack,
                               const float release) {
  if (!stream_) return;
  SDL_LockAudioStream(stream_);
  groups_.AddDuck(group, by, static_cast<double>(amount),
                  static_cast<double>(attack), static_cast<double>(release));
  SDL_UnlockAudioStream(stream_);
}

void AudioEngine::SetReverb(const float send, const float decay_seconds) {
  if (!stream_) return;
  SDL_LockAudioStream(stream_);
  reverb_.Set(static_cast<double>(send), static_cast<double>(decay_seconds));
  SDL_UnlockAudioStream(stream_);
}

void AudioEngine::SetSourceMuffle(const int id, const float muffle) {
  if (!stream_ || id < 0) return;
  SDL_LockAudioStream(stream_);
  if (static_cast<size_t>(id) < sources_.size()) {
    sources_[static_cast<size_t>(id)].target_muffle =
        std::clamp(muffle, 0.0f, 1.0f);
  }
  SDL_UnlockAudioStream(stream_);
}

void AudioEngine::ClearSources() {
  if (!stream_) return;
  SDL_LockAudioStream(stream_);
  sources_.clear();
  SDL_UnlockAudioStream(stream_);
}

void AudioEngine::PlayLoop(const std::string& name, const float gain) {
  if (!stream_ || name == current_loop_) return;
  current_loop_ = name;
  const float fade_per_sample = PerSample(kAmbienceCrossfadeSeconds, rate_);
  SDL_LockAudioStream(stream_);
  for (auto& v : voices_) {
    if (v.loop) v.fade_rate = -fade_per_sample;
  }
  const Clip* clip = FindClip(name);
  if (clip && voices_.size() < kMaxVoices) {
    Voice v;
    v.clip = clip;
    v.gain = gain;
    v.group = "ambience";
    v.loop = true;
    v.fade = 0.0f;
    v.fade_rate = fade_per_sample;
    voices_.push_back(v);
  }
  SDL_UnlockAudioStream(stream_);
}

void SDLCALL AudioEngine::Callback(void* const userdata,
                                   SDL_AudioStream* const stream,
                                   int additional, int) {
  auto* self = static_cast<AudioEngine*>(userdata);
  float buf[kMixChunkFrames * kChannels];

  const int bytes_per_frame = static_cast<int>(kChannels * sizeof(float));
  while (additional > 0) {
    const int frames = std::min(
        kMixChunkFrames, (additional + bytes_per_frame - 1) / bytes_per_frame);
    std::memset(buf, 0,
                sizeof(float) * static_cast<size_t>(frames) * kChannels);
    self->Mix(buf, frames);
    SDL_PutAudioStreamData(stream, buf, frames * bytes_per_frame);
    additional -= frames * bytes_per_frame;
  }
}

void AudioEngine::Mix(float* const out, const int frames) {
  const float duck_start = UpdateDuck(frames);
  // A group is active while one of its sounds plays; its ducks follow.
  groups_.ClearActive();
  for (const auto& v : voices_) {
    if (v.fade_rate >= 0.0f || v.fade > 0.0f) groups_.SetActive(v.group, true);
  }
  for (const auto& s : sources_) groups_.SetActive(s.group, true);
  groups_.Update(static_cast<double>(frames) / static_cast<double>(rate_));
  MixVoices(out, frames, duck_start, duck_level_);
  MixSources(out, frames);
  RemoveFinishedVoices();
  reverb_.Process(out, frames);
  for (int i = 0; i < frames * kChannels; ++i) {
    out[i] = std::max(-1.0f, std::min(1.0f, out[i]));
  }
}

float AudioEngine::UpdateDuck(const int frames) {
  float speaking = 0.0f;
  for (const auto& v : voices_) {
    if (!v.voice || v.fade_rate < 0.0f || paused_) continue;
    const float audibility =
        v.spatial
            ? std::min(1.0f, std::max(v.cur_l, v.cur_r) * kSpatialDuckPerGain)
            : 1.0f;
    speaking = std::max(speaking, audibility);
  }
  const float duck_target = 1.0f - (1.0f - voice_duck_) * speaking;
  const float duck_step =
      PerSample(kDuckEaseSeconds, rate_) * static_cast<float>(frames);
  const float duck_start = duck_level_;
  duck_level_ = duck_start < duck_target
                    ? std::min(duck_target, duck_start + duck_step)
                    : std::max(duck_target, duck_start - duck_step);
  return duck_start;
}

void AudioEngine::MixVoices(float* const out, const int frames,
                            const float duck_start, const float duck_end) {
  const float place_ease = PerSample(kGainEaseSeconds, rate_);
  for (auto& v : voices_) {
    if (paused_ && !v.loop) {
      continue;
    }
    const auto& samples = v.clip->samples;
    const auto group_gain = static_cast<float>(groups_.Gain(v.group));
    for (int i = 0; i < frames; ++i) {
      if (v.pos >= static_cast<double>(samples.size())) {
        if (!v.loop) break;
        v.pos -= static_cast<double>(samples.size());
      }
      v.fade = std::min(1.0f, std::max(0.0f, v.fade + v.fade_rate));
      float scale = 1.0f;
      if (v.loop) {
        const float t = static_cast<float>(i) / static_cast<float>(frames);
        scale = loop_scale_ * (duck_start + (duck_end - duck_start) * t);
      }
      float raw = samples[static_cast<size_t>(v.pos)];
      if (v.radio) raw = v.radio->Process(raw);
      const float sample = raw * v.gain * v.fade * scale * group_gain;
      const size_t idx = static_cast<size_t>(i) * kChannels;
      if (v.spatial) {
        v.cur_l = EaseToward(v.cur_l, v.tgt_l, place_ease);
        v.cur_r = EaseToward(v.cur_r, v.tgt_r, place_ease);
        out[idx] += sample * v.cur_l;
        out[idx + 1] += sample * v.cur_r;
      } else {
        out[idx] += sample * v.pan_l * kCentrePanCompensation;
        out[idx + 1] += sample * v.pan_r * kCentrePanCompensation;
      }
      v.pos += v.step;
    }
  }
}

void AudioEngine::MixSources(float* const out, const int frames) {
  if (paused_) return;
  const float ease = PerSample(kGainEaseSeconds, rate_);
  for (auto& src : sources_) {
    const auto& samples = src.clip->samples;
    // The filter's coefficient is set once per block from the eased muffle.
    src.muffle = EaseToward(src.muffle, src.target_muffle,
                            std::min(1.0f, ease * static_cast<float>(frames)));
    const auto group_gain = static_cast<float>(groups_.Gain(src.group));
    const bool filtered = src.muffle > 0.001f;
    const auto coefficient = static_cast<float>(LowpassCoefficient(
        MuffleCutoffHz(src.muffle), static_cast<double>(rate_)));
    for (int i = 0; i < frames; ++i) {
      if (src.pos >= static_cast<double>(samples.size())) {
        src.pos -= static_cast<double>(samples.size());
      }
      src.left = EaseToward(src.left, src.target_l, ease);
      src.right = EaseToward(src.right, src.target_r, ease);
      float sample = samples[static_cast<size_t>(src.pos)] * loop_scale_;
      if (filtered) {
        src.lowpass += coefficient * (sample - src.lowpass);
        sample = src.lowpass;
      } else {
        src.lowpass = sample;
      }
      const size_t idx = static_cast<size_t>(i) * kChannels;
      out[idx] += sample * src.left * group_gain;
      out[idx + 1] += sample * src.right * group_gain;
      src.pos += 1.0;
    }
  }
}

void AudioEngine::RemoveFinishedVoices() {
  std::erase_if(voices_, [](const Voice& v) {
    if (v.fade_rate < 0.0f && v.fade <= 0.0f) return true;
    if (v.loop) return false;
    return v.pos >= static_cast<double>(v.clip->samples.size());
  });
}
}
