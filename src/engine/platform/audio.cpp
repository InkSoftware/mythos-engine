#include "engine/platform/audio.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <numbers>
#include <string>

#include "engine/core/vfs.h"

namespace mythos {
namespace {

constexpr size_t kMaxVoices =
    24;  // one-shots, the voice line and the ambience together
constexpr float kMinPitch =
    0.25f;  // slowest playback speed a one-shot may ask for
constexpr float kVoiceFadeOutSeconds =
    0.15f;  // a cut voice line fades rather than clicks
constexpr float kAmbienceCrossfadeSeconds = 0.8f;
constexpr float kDuckEaseSeconds =
    0.3f;  // how long the ambience takes to duck or recover
constexpr float kGainEaseSeconds =
    0.06f;  // placement gains ease over this, so nothing clicks
constexpr float kSpatialDuckPerGain =
    2.0f;  // a placed line ducks fully once its louder side reaches 0.5
constexpr float kCentrePanCompensation =
    std::numbers::sqrt2_v<float>;  // sqrt(2): brings a centred one-shot back to
                                   // unit gain
constexpr int kMixChunkFrames =
    512;                      // frames mixed per pass of the audio callback
constexpr int kChannels = 2;  // the mixer's output is stereo

// Gain change per sample that takes `seconds` at `rate`.
float PerSample(float seconds, int rate) {
  return 1.0f / (seconds * static_cast<float>(rate));
}

// Moves `current` towards `target` by at most `maxStep`.
float EaseToward(float current, float target, float max_step) {
  return current + std::max(-max_step, std::min(max_step, target - current));
}

}  // namespace

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
  SDL_ResumeAudioStreamDevice(stream_);
  return true;
}

void AudioEngine::Shutdown() {
  if (stream_) {
    SDL_DestroyAudioStream(stream_);  // closes the device it opened
    stream_ = nullptr;
  }
}

int AudioEngine::LoadDir(const std::string& dir,
                         const std::vector<std::string>& names) {
  int loaded = 0;
  for (const auto& name : names) {
    const std::string path = dir + "/" + name + ".wav";
    SDL_AudioSpec spec{};
    Uint8* data = nullptr;
    Uint32 len = 0;
    std::string file;
    if (!vfs::ReadFile(path, file)) continue;
    SDL_IOStream* io = SDL_IOFromConstMem(file.data(), file.size());
    if (!io || !SDL_LoadWAV_IO(io, true, &spec, &data, &len)) continue;
    // The mixer wants mono float at the device rate (the stream converts to the
    // device).
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
    const float* converted = reinterpret_cast<const float*>(out);
    clip.samples.assign(converted, converted + out_len / sizeof(float));
    SDL_free(out);
    if (clip.samples.empty()) continue;
    SDL_LockAudioStream(stream_);
    clips_[name] = std::move(clip);
    SDL_UnlockAudioStream(stream_);
    ++loaded;
  }
  return loaded;
}

void AudioEngine::Play(const std::string& name, float gain, float pitch) {
  if (!stream_) return;
  const Clip* clip = FindClip(name);
  if (!clip) return;
  Voice v;
  v.clip = clip;
  v.step = std::max(kMinPitch, pitch);
  v.gain = gain * effect_scale_;
  StartVoice(v);
}

void AudioEngine::PlayVoice(const std::string& name, float gain, bool spatial,
                            bool radio) {
  if (!stream_) return;
  StopVoice();
  const Clip* clip = FindClip(name);
  if (!clip) return;
  Voice v;
  v.clip = clip;
  v.gain = gain * effect_scale_;
  v.voice = true;
  v.spatial = spatial;
  if (radio) {
    // A different crackle every time: seeded from how many lines have played.
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

void AudioEngine::SetVoicePlacement(float left, float right) {
  if (!stream_) return;
  SDL_LockAudioStream(stream_);
  for (auto& v : voices_) {
    if (!v.voice || !v.spatial || v.fade_rate < 0.0f) continue;
    v.tgt_l = std::max(0.0f, left);
    v.tgt_r = std::max(0.0f, right);
    if (!v.placed) {  // the first placement is where it starts: no ramp up from
                      // silence
      v.placed = true;
      v.cur_l = v.tgt_l;
      v.cur_r = v.tgt_r;
    }
  }
  SDL_UnlockAudioStream(stream_);
}

void AudioEngine::SetPaused(bool paused) {
  if (!stream_) return;
  SDL_LockAudioStream(stream_);
  paused_ = paused;
  SDL_UnlockAudioStream(stream_);
}

void AudioEngine::SetVoiceDuck(float duck) {
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

void AudioEngine::SetVolumes(float master, float ambience, float effects) {
  if (!stream_) return;
  SDL_LockAudioStream(stream_);
  effect_scale_ = master * effects;
  loop_scale_ = master * ambience;
  SDL_UnlockAudioStream(stream_);
}

int AudioEngine::AddSource(const std::string& name) {
  if (!stream_) return -1;
  const Clip* clip = FindClip(name);
  if (!clip || clip->samples.empty()) return -1;
  Source s;
  s.clip = clip;
  SDL_LockAudioStream(stream_);
  sources_.push_back(s);
  const int id = static_cast<int>(sources_.size()) - 1;
  SDL_UnlockAudioStream(stream_);
  return id;
}

void AudioEngine::SetSource(int id, float left, float right) {
  if (!stream_ || id < 0) return;
  SDL_LockAudioStream(stream_);
  if (static_cast<size_t>(id) < sources_.size()) {
    sources_[static_cast<size_t>(id)].target_l = std::max(0.0f, left);
    sources_[static_cast<size_t>(id)].target_r = std::max(0.0f, right);
  }
  SDL_UnlockAudioStream(stream_);
}

void AudioEngine::ClearSources() {
  if (!stream_) return;
  SDL_LockAudioStream(stream_);
  sources_.clear();
  SDL_UnlockAudioStream(stream_);
}

void AudioEngine::PlayLoop(const std::string& name, float gain) {
  if (!stream_ || name == current_loop_) return;
  current_loop_ = name;
  const float fade_per_sample = PerSample(kAmbienceCrossfadeSeconds, rate_);
  SDL_LockAudioStream(stream_);
  for (auto& v : voices_) {
    if (v.loop) v.fade_rate = -fade_per_sample;  // fade the old ambience out
  }
  const Clip* clip = FindClip(name);
  if (clip && voices_.size() < kMaxVoices) {
    Voice v;
    v.clip = clip;
    v.gain = gain;
    v.loop = true;
    v.fade = 0.0f;
    v.fade_rate = fade_per_sample;
    voices_.push_back(v);
  }
  SDL_UnlockAudioStream(stream_);
}

void SDLCALL AudioEngine::Callback(void* userdata, SDL_AudioStream* stream,
                                   int additional, int /*total*/) {
  auto* self = static_cast<AudioEngine*>(userdata);
  float buf[kMixChunkFrames * kChannels];  // mixed in chunks; the stream is
                                           // locked by SDL while this runs
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

void AudioEngine::Mix(float* out, int frames) {
  const float duck_start = UpdateDuck(frames);
  MixVoices(out, frames, duck_start, duck_level_);
  MixSources(out, frames);
  RemoveFinishedVoices();
  for (int i = 0; i < frames * kChannels; ++i) {
    out[i] = std::max(-1.0f, std::min(1.0f, out[i]));
  }
}

float AudioEngine::UpdateDuck(int frames) {
  // The ambience ducks (over ~0.3s) while a voice line is playing and comes
  // back after. A line placed in the world ducks it only as much as it is
  // audible; a paused line not at all.
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

void AudioEngine::MixVoices(float* out, int frames, float duck_start,
                            float duck_end) {
  const float place_ease = PerSample(kGainEaseSeconds, rate_);
  for (auto& v : voices_) {
    if (paused_ && !v.loop) {
      continue;  // frozen: it carries on from this sample when resumed
    }
    const auto& samples = v.clip->samples;
    for (int i = 0; i < frames; ++i) {
      if (v.pos >= static_cast<double>(samples.size())) {
        if (!v.loop) break;
        v.pos -= static_cast<double>(samples.size());
      }
      v.fade = std::min(1.0f, std::max(0.0f, v.fade + v.fade_rate));
      float scale = 1.0f;
      if (v.loop) {  // the ambience: live volume, and the duck ramped across
                     // the chunk
        const float t = static_cast<float>(i) / static_cast<float>(frames);
        scale = loop_scale_ * (duck_start + (duck_end - duck_start) * t);
      }
      float raw = samples[static_cast<size_t>(v.pos)];
      if (v.radio) raw = v.radio->Process(raw);
      const float sample = raw * v.gain * v.fade * scale;
      if (v.spatial) {
        v.cur_l = EaseToward(v.cur_l, v.tgt_l, place_ease);
        v.cur_r = EaseToward(v.cur_r, v.tgt_r, place_ease);
        out[i * kChannels] += sample * v.cur_l;
        out[i * kChannels + 1] += sample * v.cur_r;
      } else {
        out[i * kChannels] += sample * v.pan_l * kCentrePanCompensation;
        out[i * kChannels + 1] += sample * v.pan_r * kCentrePanCompensation;
      }
      v.pos += v.step;
    }
  }
}

void AudioEngine::MixSources(float* out, int frames) {
  // Positional sources: looping, their gains eased (~60ms) towards what the
  // frame set.
  if (paused_) return;
  const float ease = PerSample(kGainEaseSeconds, rate_);
  for (auto& src : sources_) {
    const auto& samples = src.clip->samples;
    for (int i = 0; i < frames; ++i) {
      if (src.pos >= static_cast<double>(samples.size())) {
        src.pos -= static_cast<double>(samples.size());
      }
      src.left = EaseToward(src.left, src.target_l, ease);
      src.right = EaseToward(src.right, src.target_r, ease);
      const float sample = samples[static_cast<size_t>(src.pos)] * loop_scale_;
      out[i * kChannels] += sample * src.left;
      out[i * kChannels + 1] += sample * src.right;
      src.pos += 1.0;
    }
  }
}

void AudioEngine::RemoveFinishedVoices() {
  std::erase_if(voices_, [](const Voice& v) {
    if (v.fade_rate < 0.0f && v.fade <= 0.0f) return true;  // faded out
    if (v.loop) return false;
    return v.pos >= static_cast<double>(v.clip->samples.size());
  });
}

}  // namespace mythos
