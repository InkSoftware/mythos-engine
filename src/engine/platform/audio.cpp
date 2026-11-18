#include "engine/platform/audio.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>

#include "engine/core/vfs.h"

namespace mythos {
namespace {

constexpr size_t kMaxVoices = 24;                 // one-shots, the voice line and the ambience together
constexpr float kMinPitch = 0.25f;             // slowest playback speed a one-shot may ask for
constexpr float kVoiceFadeOutSeconds = 0.15f;  // a cut voice line fades rather than clicks
constexpr float kAmbienceCrossfadeSeconds = 0.8f;
constexpr float kDuckEaseSeconds = 0.3f;       // how long the ambience takes to duck or recover
constexpr float kGainEaseSeconds = 0.06f;      // placement gains ease over this, so nothing clicks
constexpr float kSpatialDuckPerGain = 2.0f;    // a placed line ducks fully once its louder side reaches 0.5
constexpr float kCentrePanCompensation = 1.4142f;  // sqrt(2): brings a centred one-shot back to unit gain
constexpr int kMixChunkFrames = 512;           // frames mixed per pass of the audio callback
constexpr int kChannels = 2;                   // the mixer's output is stereo

// Gain change per sample that takes `seconds` at `rate`.
float perSample(float seconds, int rate) { return 1.0f / (seconds * static_cast<float>(rate)); }

// Moves `current` towards `target` by at most `maxStep`.
float easeToward(float current, float target, float maxStep) {
    return current + std::max(-maxStep, std::min(maxStep, target - current));
}

}  // namespace

bool AudioEngine::init() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
    SDL_AudioSpec want{};
    want.freq = rate_;
    want.format = SDL_AUDIO_F32;
    want.channels = kChannels;
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, &AudioEngine::callback, this);
    if (!stream_) return false;
    SDL_AudioSpec have{};
    int frames = 0;
    if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream_), &have, &frames) && have.freq > 0) rate_ = have.freq;
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void AudioEngine::shutdown() {
    if (stream_) {
        SDL_DestroyAudioStream(stream_);  // closes the device it opened
        stream_ = nullptr;
    }
}

int AudioEngine::loadDir(const std::string& dir, const std::vector<std::string>& names) {
    int loaded = 0;
    for (const auto& name : names) {
        const std::string path = dir + "/" + name + ".wav";
        SDL_AudioSpec spec{};
        Uint8* data = nullptr;
        Uint32 len = 0;
        std::string file;
        if (!vfs::readFile(path, file)) continue;
        SDL_IOStream* io = SDL_IOFromConstMem(file.data(), file.size());
        if (!io || !SDL_LoadWAV_IO(io, true, &spec, &data, &len)) continue;
        // The mixer wants mono float at the device rate (the stream converts to the device).
        SDL_AudioSpec target{};
        target.format = SDL_AUDIO_F32;
        target.channels = 1;
        target.freq = rate_;
        Uint8* out = nullptr;
        int outLen = 0;
        const bool ok = SDL_ConvertAudioSamples(&spec, data, static_cast<int>(len), &target, &out, &outLen);
        SDL_free(data);
        if (!ok) continue;
        Clip clip;
        const float* converted = reinterpret_cast<const float*>(out);
        clip.samples.assign(converted, converted + outLen / sizeof(float));
        SDL_free(out);
        if (clip.samples.empty()) continue;
        SDL_LockAudioStream(stream_);
        clips_[name] = std::move(clip);
        SDL_UnlockAudioStream(stream_);
        ++loaded;
    }
    return loaded;
}

void AudioEngine::play(const std::string& name, float gain, float pitch) {
    if (!stream_) return;
    const Clip* clip = findClip(name);
    if (!clip) return;
    Voice v;
    v.clip = clip;
    v.step = std::max(kMinPitch, pitch);
    v.gain = gain * effectScale_;
    startVoice(v);
}

void AudioEngine::playVoice(const std::string& name, float gain, bool spatial) {
    if (!stream_) return;
    stopVoice();
    const Clip* clip = findClip(name);
    if (!clip) return;
    Voice v;
    v.clip = clip;
    v.gain = gain * effectScale_;
    v.voice = true;
    v.spatial = spatial;
    startVoice(v);
}

void AudioEngine::startVoice(const Voice& voice) {
    SDL_LockAudioStream(stream_);
    if (voices_.size() < kMaxVoices) voices_.push_back(voice);
    SDL_UnlockAudioStream(stream_);
}

const AudioEngine::Clip* AudioEngine::findClip(const std::string& name) const {
    auto it = clips_.find(name);
    return it == clips_.end() ? nullptr : &it->second;
}

void AudioEngine::stopVoice() {
    if (!stream_) return;
    const float fadePerSample = perSample(kVoiceFadeOutSeconds, rate_);
    SDL_LockAudioStream(stream_);
    for (auto& v : voices_)
        if (v.voice) v.fadeRate = -fadePerSample;
    SDL_UnlockAudioStream(stream_);
}

void AudioEngine::setVoicePlacement(float left, float right) {
    if (!stream_) return;
    SDL_LockAudioStream(stream_);
    for (auto& v : voices_) {
        if (!v.voice || !v.spatial || v.fadeRate < 0.0f) continue;
        v.tgtL = std::max(0.0f, left);
        v.tgtR = std::max(0.0f, right);
        if (!v.placed) {  // the first placement is where it starts: no ramp up from silence
            v.placed = true;
            v.curL = v.tgtL;
            v.curR = v.tgtR;
        }
    }
    SDL_UnlockAudioStream(stream_);
}

void AudioEngine::setPaused(bool paused) {
    if (!stream_) return;
    SDL_LockAudioStream(stream_);
    paused_ = paused;
    SDL_UnlockAudioStream(stream_);
}

void AudioEngine::setVoiceDuck(float duck) {
    if (!stream_) return;
    SDL_LockAudioStream(stream_);
    voiceDuck_ = std::max(0.0f, std::min(1.0f, duck));
    SDL_UnlockAudioStream(stream_);
}

double AudioEngine::clipSeconds(const std::string& name) const {
    auto it = clips_.find(name);
    if (it == clips_.end() || rate_ <= 0) return 0.0;
    return static_cast<double>(it->second.samples.size()) / rate_;
}

void AudioEngine::setVolumes(float master, float ambience, float effects) {
    if (!stream_) return;
    SDL_LockAudioStream(stream_);
    effectScale_ = master * effects;
    loopScale_ = master * ambience;
    SDL_UnlockAudioStream(stream_);
}

int AudioEngine::addSource(const std::string& name) {
    if (!stream_) return -1;
    const Clip* clip = findClip(name);
    if (!clip || clip->samples.empty()) return -1;
    Source s;
    s.clip = clip;
    SDL_LockAudioStream(stream_);
    sources_.push_back(s);
    const int id = static_cast<int>(sources_.size()) - 1;
    SDL_UnlockAudioStream(stream_);
    return id;
}

void AudioEngine::setSource(int id, float left, float right) {
    if (!stream_ || id < 0) return;
    SDL_LockAudioStream(stream_);
    if (static_cast<size_t>(id) < sources_.size()) {
        sources_[static_cast<size_t>(id)].targetL = std::max(0.0f, left);
        sources_[static_cast<size_t>(id)].targetR = std::max(0.0f, right);
    }
    SDL_UnlockAudioStream(stream_);
}

void AudioEngine::clearSources() {
    if (!stream_) return;
    SDL_LockAudioStream(stream_);
    sources_.clear();
    SDL_UnlockAudioStream(stream_);
}

void AudioEngine::playLoop(const std::string& name, float gain) {
    if (!stream_ || name == currentLoop_) return;
    currentLoop_ = name;
    const float fadePerSample = perSample(kAmbienceCrossfadeSeconds, rate_);
    SDL_LockAudioStream(stream_);
    for (auto& v : voices_)
        if (v.loop) v.fadeRate = -fadePerSample;  // fade the old ambience out
    const Clip* clip = findClip(name);
    if (clip && voices_.size() < kMaxVoices) {
        Voice v;
        v.clip = clip;
        v.gain = gain;
        v.loop = true;
        v.fade = 0.0f;
        v.fadeRate = fadePerSample;
        voices_.push_back(v);
    }
    SDL_UnlockAudioStream(stream_);
}

void SDLCALL AudioEngine::callback(void* userdata, SDL_AudioStream* stream, int additional, int /*total*/) {
    auto* self = static_cast<AudioEngine*>(userdata);
    float buf[kMixChunkFrames * kChannels];  // mixed in chunks; the stream is locked by SDL while this runs
    const int bytesPerFrame = static_cast<int>(kChannels * sizeof(float));
    while (additional > 0) {
        const int frames = std::min(kMixChunkFrames, (additional + bytesPerFrame - 1) / bytesPerFrame);
        std::memset(buf, 0, sizeof(float) * static_cast<size_t>(frames) * kChannels);
        self->mix(buf, frames);
        SDL_PutAudioStreamData(stream, buf, frames * bytesPerFrame);
        additional -= frames * bytesPerFrame;
    }
}

void AudioEngine::mix(float* out, int frames) {
    const float duckStart = updateDuck(frames);
    mixVoices(out, frames, duckStart, duckLevel_);
    mixSources(out, frames);
    removeFinishedVoices();
    for (int i = 0; i < frames * kChannels; ++i) out[i] = std::max(-1.0f, std::min(1.0f, out[i]));
}

float AudioEngine::updateDuck(int frames) {
    // The ambience ducks (over ~0.3s) while a voice line is playing and comes back after.
    // A line placed in the world ducks it only as much as it is audible; a paused line not at all.
    float speaking = 0.0f;
    for (const auto& v : voices_) {
        if (!v.voice || v.fadeRate < 0.0f || paused_) continue;
        const float audibility = v.spatial ? std::min(1.0f, std::max(v.curL, v.curR) * kSpatialDuckPerGain) : 1.0f;
        speaking = std::max(speaking, audibility);
    }
    const float duckTarget = 1.0f - (1.0f - voiceDuck_) * speaking;
    const float duckStep = perSample(kDuckEaseSeconds, rate_) * static_cast<float>(frames);
    const float duckStart = duckLevel_;
    duckLevel_ = duckStart < duckTarget ? std::min(duckTarget, duckStart + duckStep)
                                        : std::max(duckTarget, duckStart - duckStep);
    return duckStart;
}

void AudioEngine::mixVoices(float* out, int frames, float duckStart, float duckEnd) {
    const float placeEase = perSample(kGainEaseSeconds, rate_);
    for (auto& v : voices_) {
        if (paused_ && !v.loop) continue;  // frozen: it carries on from this sample when resumed
        const auto& samples = v.clip->samples;
        for (int i = 0; i < frames; ++i) {
            if (v.pos >= static_cast<double>(samples.size())) {
                if (!v.loop) break;
                v.pos -= static_cast<double>(samples.size());
            }
            v.fade = std::min(1.0f, std::max(0.0f, v.fade + v.fadeRate));
            float scale = 1.0f;
            if (v.loop) {  // the ambience: live volume, and the duck ramped across the chunk
                const float t = static_cast<float>(i) / static_cast<float>(frames);
                scale = loopScale_ * (duckStart + (duckEnd - duckStart) * t);
            }
            const float sample = samples[static_cast<size_t>(v.pos)] * v.gain * v.fade * scale;
            if (v.spatial) {
                v.curL = easeToward(v.curL, v.tgtL, placeEase);
                v.curR = easeToward(v.curR, v.tgtR, placeEase);
                out[i * kChannels] += sample * v.curL;
                out[i * kChannels + 1] += sample * v.curR;
            } else {
                out[i * kChannels] += sample * v.panL * kCentrePanCompensation;
                out[i * kChannels + 1] += sample * v.panR * kCentrePanCompensation;
            }
            v.pos += v.step;
        }
    }
}

void AudioEngine::mixSources(float* out, int frames) {
    // Positional sources: looping, their gains eased (~60ms) towards what the frame set.
    if (paused_) return;
    const float ease = perSample(kGainEaseSeconds, rate_);
    for (auto& src : sources_) {
        const auto& samples = src.clip->samples;
        for (int i = 0; i < frames; ++i) {
            if (src.pos >= static_cast<double>(samples.size())) src.pos -= static_cast<double>(samples.size());
            src.left = easeToward(src.left, src.targetL, ease);
            src.right = easeToward(src.right, src.targetR, ease);
            const float sample = samples[static_cast<size_t>(src.pos)] * loopScale_;
            out[i * kChannels] += sample * src.left;
            out[i * kChannels + 1] += sample * src.right;
            src.pos += 1.0;
        }
    }
}

void AudioEngine::removeFinishedVoices() {
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) {
                                     if (v.fadeRate < 0.0f && v.fade <= 0.0f) return true;  // faded out
                                     if (v.loop) return false;
                                     return v.pos >= static_cast<double>(v.clip->samples.size());
                                 }),
                  voices_.end());
}

}  // namespace mythos
