#ifndef MYTHOS_ENGINE_PLATFORM_AUDIO_H_
#define MYTHOS_ENGINE_PLATFORM_AUDIO_H_

#include <SDL3/SDL.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/audio/radio_fx.h"
#include "engine/audio/reverb.h"

namespace mythos {
// Stereo float mixer on an SDL audio stream. Clips are mono WAVs resampled to
// the device rate at load. Mixing runs on SDL's audio thread; every public
// method that touches mixer state locks the stream, so they may be called from
// the game thread. All methods are no-ops before a successful Init().
//
// Sound kinds: one-shots (`Play`), one crossfading ambience loop (`PlayLoop`),
// at most one spoken line at a time (`PlayVoice`, which ducks the ambience) and
// looping positional sources (`AddSource`).
class AudioEngine {
 public:
  ~AudioEngine() { Shutdown(); }

  // Opens the default playback device. Returns false if audio is unavailable.
  bool Init();

  // Closes the device; idempotent.
  void Shutdown();

  // Loads `<dir>/<name>.wav` through the VFS for each name; unreadable files
  // are skipped. Returns the number loaded.
  int LoadDir(const std::string& dir, const std::vector<std::string>& names);

  // Plays a one-shot at `gain` (scaled by master x effects volume). `pitch` is
  // a playback-speed ratio, clamped to at least 0.25.
  void Play(const std::string& name, float gain = 1.0f, float pitch = 1.0f);

  // Crossfades to the ambience loop `name`; repeating the current one does
  // nothing.
  void PlayLoop(const std::string& name, float gain = 1.0f);

  // Adds a looping positional source and returns its id, or -1 on failure. Ids
  // are valid until ClearSources().
  int AddSource(const std::string& name);

  // Sets a source's target left/right gains; the mixer eases toward them.
  void SetSource(int id, float left, float right);

  // Sets how muffled a source sounds (0 clear .. 1 fully; see ComputeOcclusion
  // in audio/spatial.h); the mixer eases toward it and low-passes the source.
  void SetSourceMuffle(int id, float muffle);

  // Removes every positional source.
  void ClearSources();

  // Starts a spoken line, fading out any current one. With `spatial` its pan
  // follows SetVoicePlacement(); with `radio` it goes through the FM radio
  // glitch effect.
  void PlayVoice(const std::string& name, float gain = 1.0f,
                 bool spatial = false, bool radio = false);

  // Fades out the current spoken line.
  void StopVoice();

  // Sets the target stereo gains of the current spatial voice.
  void SetVoicePlacement(float left, float right);

  // While paused only the ambience loop keeps playing.
  void SetPaused(bool paused);

  // Sets how far ambience is lowered while a voice speaks (1 = not at all, 0 =
  // silent); clamped to 0..1.
  void SetVoiceDuck(float duck);

  // Length of a loaded clip in seconds, or 0 if unknown.
  [[nodiscard]] double ClipSeconds(const std::string& name) const;

  // The room's reverb: how much is added to the whole mix (0 off) and how long
  // the tail rings in seconds; eased, so walking between rooms does not click.
  // `ReverbForRoom` (audio/spatial.h) gives both from the size of a room.
  void SetReverb(float send, float decay_seconds);

  // Volumes as 0..1 factors; master multiplies the other two.
  void SetVolumes(float master, float ambience, float effects);

 private:
  struct Clip {
    std::vector<float> samples;
  };

  // One playing sound (one-shot, loop or spoken line). `fade` runs 0..1 at
  // `fade_rate` per sample; a negative rate retires the voice once it hits 0.
  struct Voice {
    const Clip* clip = nullptr;
    double pos = 0;
    double step = 1.0;
    float gain = 1.0f;
    bool loop = false;
    bool voice = false;
    float fade = 1.0f;
    float fade_rate = 0.0f;
    float pan_l = 0.7071f, pan_r = 0.7071f;

    bool spatial = false;
    bool placed = false;
    float cur_l = 0, cur_r = 0;
    float tgt_l = 0, tgt_r = 0;
    std::shared_ptr<RadioFm> radio;
  };

  // A looping positional sound with eased left/right gains.
  struct Source {
    const Clip* clip = nullptr;
    double pos = 0;
    float left = 0, right = 0;
    float target_l = 0, target_r = 0;
    // Low-pass for occlusion: the muffle (0 clear .. 1 closed) and its target,
    // and the filter state.
    float muffle = 0, target_muffle = 0, lowpass = 0;
  };

  // SDL pulls audio here on the audio thread.
  static void SDLCALL Callback(void* userdata, SDL_AudioStream* stream,
                               int additional, int total);

  void Mix(float* out, int frames);

  // Eases the ambience duck level toward its target for this chunk and returns
  // the level at the chunk's start.
  float UpdateDuck(int frames);

  void MixVoices(float* out, int frames, float duck_start, float duck_end);

  void MixSources(float* out, int frames);

  void RemoveFinishedVoices();

  void StartVoice(const Voice& voice);

  [[nodiscard]] const Clip* FindClip(const std::string& name) const;

  Reverb reverb_;
  SDL_AudioStream* stream_ = nullptr;
  int rate_ = 44100;
  std::map<std::string, Clip> clips_;
  // Mixer state below is guarded by the audio stream lock.
  std::vector<Voice> voices_;
  std::vector<Source> sources_;
  std::string current_loop_;
  float effect_scale_ = 1.0f;
  float loop_scale_ = 1.0f;
  float voice_duck_ = 1.0f;
  float duck_level_ = 1.0f;
  bool paused_ = false;
  uint32_t radio_lines_ = 0;
};
}

#endif
