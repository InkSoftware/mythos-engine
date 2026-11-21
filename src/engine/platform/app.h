#ifndef MYTHOS_ENGINE_PLATFORM_APP_H_
#define MYTHOS_ENGINE_PLATFORM_APP_H_

#include <SDL3/SDL.h>

#include <map>
#include <string>
#include <vector>

#include "engine/audio/audio_math.h"
#include "engine/platform/audio.h"
#include "engine/platform/window.h"
#include "engine/ui/ui.h"

namespace mythos {
// Per-user writable files (settings, saves) under the SDL preference folder for
// an organization and application name.
class UserFiles {
 public:
  UserFiles(std::string org, std::string app)
      : org_(std::move(org)), app_(std::move(app)) {}

  // Full path of `file` in the preference folder (just `file` if SDL has none).
  [[nodiscard]] std::string Path(const std::string& file) const {
    return PrefFilePath(org_, app_, file);
  }

  // Returns the file's text, or an empty string if it cannot be read.
  [[nodiscard]] std::string Read(const std::string& file) const;

  // Writes atomically and logs on failure. With `remove_if_empty` an empty
  // `text` deletes the file instead. Returns false on failure.
  [[nodiscard]] bool Write(const std::string& file, const std::string& text,
                           bool remove_if_empty = false) const;

 private:
  std::string org_, app_;
};

// Measures frame time with the SDL performance counter and accumulates total
// elapsed time.
class FrameClock {
 public:
  FrameClock()
      : freq_(SDL_GetPerformanceFrequency()),
        last_(SDL_GetPerformanceCounter()) {}

  // Advances the clock and returns the frame's delta in seconds. With `fixed60`
  // the delta is a constant 1/60 s (for deterministic runs and captures).
  double Step(const bool fixed60) {
    const Uint64 now = SDL_GetPerformanceCounter();
    const double dt =
        fixed60 ? 1.0 / 60.0
                : static_cast<double>(now - last_) / static_cast<double>(freq_);
    last_ = now;
    seconds_ += dt;
    return dt;
  }

  [[nodiscard]] double Seconds() const { return seconds_; }

 private:
  Uint64 freq_, last_;
  double seconds_ = 0.0;
};

// Caps the frame rate by sleeping until a running deadline.
class FramePacer {
 public:
  // Sleeps until the next frame is due at `fps`; 0 or less means unlimited. The
  // deadline is reset when the loop has fallen more than four frames behind, so
  // a stall does not cause a burst of catch-up frames.
  void Wait(const int fps) {
    if (fps <= 0) {
      deadline_ = 0;
      return;
    }
    const Uint64 now = SDL_GetPerformanceCounter();
    const Uint64 frame =
        SDL_GetPerformanceFrequency() / static_cast<Uint64>(fps);
    if (deadline_ == 0 || now > deadline_ + frame * 4) {
      deadline_ = now;
    }
    deadline_ += frame;
    if (deadline_ > now) {
      SDL_DelayPrecise((deadline_ - now) * 1000000000ull /
                       SDL_GetPerformanceFrequency());
    }
  }

 private:
  Uint64 deadline_ = 0;
};

// Owns the arrow and hand system cursors and maps window pixels to overlay
// hotspots.
class MousePointer {
 public:
  MousePointer();

  ~MousePointer();

  MousePointer(const MousePointer&) = delete;

  MousePointer& operator=(const MousePointer&) = delete;

  // Hotspot index under window position (mx, my), or -1. The position is scaled
  // from window size to the overlay's resolution; `value` receives the
  // hotspot's value when it is hit.
  static int ItemAt(const Window& window, int overlay_w, int overlay_h,
                    const std::vector<Hotspot>& hotspots, int mx, int my,
                    int* value = nullptr);

  // Shows the hand cursor over a hotspot and the arrow elsewhere.
  void UpdateCursor(const Window& window, int overlay_w, int overlay_h,
                    const std::vector<Hotspot>& hotspots) const;

 private:
  SDL_Cursor* arrow_ = nullptr;
  SDL_Cursor* hand_ = nullptr;
};

// Picks a clip, gain and pitch variation from `cue` and plays it. Does nothing
// for a cue without clips.
void PlayCue(AudioEngine& audio, const SoundCue& cue, Rng& rng);

// Lengths in seconds of the loaded clips among `names`; unknown or empty clips
// are left out.
std::map<std::string, double> ClipLengths(
    const AudioEngine& audio, const std::vector<std::string>& names);

// Mounts `assets.pak` found beside the executable (or in a macOS bundle's
// Resources) into the virtual file system, decrypting with a key derived from
// `pack_key`. Returns the virtual `assets` root, or an empty string when there
// is no pack or it cannot be mounted.
std::string MountAssetPack(const std::string& pack_key);
}

#endif
