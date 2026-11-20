#ifndef MYTHOS_ENGINE_PLATFORM_APP_H_
#define MYTHOS_ENGINE_PLATFORM_APP_H_
// The odds and ends every game's front end needs: per-user files, the frame
// clock, the menu pointer, and playing a sound cue.
#include <SDL3/SDL.h>

#include <map>
#include <string>
#include <vector>

#include "engine/audio/audio_math.h"
#include "engine/platform/audio.h"
#include "engine/platform/window.h"
#include "engine/ui/ui.h"

namespace mythos {

// Files in the user's preferences folder for one game (settings, the save, the
// profile).
class UserFiles {
 public:
  UserFiles(std::string org, std::string app)
      : org_(std::move(org)), app_(std::move(app)) {}
  std::string Path(const std::string& file) const {
    return PrefFilePath(org_, app_, file);
  }
  std::string Read(const std::string& file) const;
  // Writes atomically; an empty text removes the file when removeIfEmpty. Logs
  // on failure.
  bool Write(const std::string& file, const std::string& text,
             bool remove_if_empty = false) const;

 private:
  std::string org_, app_;
};

// Seconds since the last step. A fixed step (1/60) makes screenshot runs
// repeatable.
class FrameClock {
 public:
  FrameClock()
      : freq_(SDL_GetPerformanceFrequency()),
        last_(SDL_GetPerformanceCounter()) {}
  double Step(bool fixed60) {
    const Uint64 now = SDL_GetPerformanceCounter();
    const double dt =
        fixed60 ? 1.0 / 60.0
                : static_cast<double>(now - last_) / static_cast<double>(freq_);
    last_ = now;
    seconds_ += dt;
    return dt;
  }
  double Seconds() const { return seconds_; }  // total time stepped

 private:
  Uint64 freq_, last_;
  double seconds_ = 0.0;
};

// Holds a loop to a frame rate: call wait(fps) once per frame, after swapping.
// 0 does nothing.
class FramePacer {
 public:
  void Wait(int fps) {
    if (fps <= 0) {
      deadline_ = 0;
      return;
    }
    const Uint64 now = SDL_GetPerformanceCounter();
    const Uint64 frame =
        SDL_GetPerformanceFrequency() / static_cast<Uint64>(fps);
    if (deadline_ == 0 || now > deadline_ + frame * 4) {
      deadline_ = now;  // first frame, or far behind: restart
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

// The mouse on the menu pages: window coordinates -> overlay pixels -> the
// hotspot there, and a hand cursor over anything clickable.
class MousePointer {
 public:
  MousePointer();
  ~MousePointer();
  MousePointer(const MousePointer&) = delete;
  MousePointer& operator=(const MousePointer&) = delete;
  // The item under window position (mx, my), given the overlay's size and last
  // frame's hotspots.
  static int ItemAt(const Window& window, int overlay_w, int overlay_h,
                    const std::vector<Hotspot>& hotspots, int mx, int my,
                    int* value = nullptr);
  // Shows the hand when the pointer is over an item, the arrow otherwise.
  void UpdateCursor(const Window& window, int overlay_w, int overlay_h,
                    const std::vector<Hotspot>& hotspots) const;

 private:
  SDL_Cursor* arrow_ = nullptr;
  SDL_Cursor* hand_ = nullptr;
};

// Plays a sound cue: its chosen clip at its gain, with the pitch variation it
// asks for.
void PlayCue(AudioEngine& audio, const SoundCue& cue, Rng& rng);

// The lengths in seconds of the loaded clips among `names` (clips that did not
// load are left out).
std::map<std::string, double> ClipLengths(
    const AudioEngine& audio, const std::vector<std::string>& names);

// A packed build ships assets.pak next to the executable (or, in a macOS
// bundle, in ../Resources) instead of an assets folder. Mounts it under the key
// `pack_key` (the one mythos_pack was given) and returns the virtual "assets"
// folder every asset read should go below, so the files are not on disk to
// edit. Empty if there is no pack or it does not open.
std::string MountAssetPack(const std::string& pack_key);

}  // namespace mythos

#endif  // MYTHOS_ENGINE_PLATFORM_APP_H_
