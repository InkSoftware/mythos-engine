#pragma once
// The odds and ends every game's front end needs: per-user files, the frame clock, the menu
// pointer, and playing a sound cue.
#include <map>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "engine/audio/audio_math.h"
#include "engine/platform/audio.h"
#include "engine/platform/window.h"
#include "engine/ui/ui.h"

namespace mythos {

// Files in the user's preferences folder for one game (settings, the save, the profile).
class UserFiles {
public:
    UserFiles(std::string org, std::string app) : org_(std::move(org)), app_(std::move(app)) {}
    std::string path(const std::string& file) const { return prefFilePath(org_, app_, file); }
    std::string read(const std::string& file) const;
    // Writes atomically; an empty text removes the file when removeIfEmpty. Logs on failure.
    bool write(const std::string& file, const std::string& text, bool removeIfEmpty = false) const;

private:
    std::string org_, app_;
};

// Seconds since the last step. A fixed step (1/60) makes screenshot runs repeatable.
class FrameClock {
public:
    FrameClock() : freq_(SDL_GetPerformanceFrequency()), last_(SDL_GetPerformanceCounter()) {}
    double step(bool fixed60) {
        const Uint64 now = SDL_GetPerformanceCounter();
        const double dt = fixed60 ? 1.0 / 60.0 : static_cast<double>(now - last_) / static_cast<double>(freq_);
        last_ = now;
        seconds_ += dt;
        return dt;
    }
    double seconds() const { return seconds_; }  // total time stepped

private:
    Uint64 freq_, last_;
    double seconds_ = 0.0;
};

// Holds a loop to a frame rate: call wait(fps) once per frame, after swapping. 0 does nothing.
class FramePacer {
public:
    void wait(int fps) {
        if (fps <= 0) {
            deadline_ = 0;
            return;
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        const Uint64 frame = SDL_GetPerformanceFrequency() / static_cast<Uint64>(fps);
        if (deadline_ == 0 || now > deadline_ + frame * 4) deadline_ = now;  // first frame, or far behind: restart
        deadline_ += frame;
        if (deadline_ > now) SDL_DelayPrecise((deadline_ - now) * 1000000000ull / SDL_GetPerformanceFrequency());
    }

private:
    Uint64 deadline_ = 0;
};

// The mouse on the menu pages: window coordinates -> overlay pixels -> the hotspot there, and a
// hand cursor over anything clickable.
class MousePointer {
public:
    MousePointer();
    ~MousePointer();
    MousePointer(const MousePointer&) = delete;
    MousePointer& operator=(const MousePointer&) = delete;
    // The item under window position (mx, my), given the overlay's size and last frame's hotspots.
    int itemAt(const Window& window, int overlayW, int overlayH, const std::vector<Hotspot>& hotspots, int mx, int my,
               int* value = nullptr) const;
    // Shows the hand when the pointer is over an item, the arrow otherwise.
    void updateCursor(const Window& window, int overlayW, int overlayH, const std::vector<Hotspot>& hotspots) const;

private:
    SDL_Cursor* arrow_ = nullptr;
    SDL_Cursor* hand_ = nullptr;
};

// Plays a sound cue: its chosen clip at its gain, with the pitch variation it asks for.
void playCue(AudioEngine& audio, const SoundCue& cue, Rng& rng);

// The lengths in seconds of the loaded clips among `names` (clips that did not load are left out).
std::map<std::string, double> clipLengths(const AudioEngine& audio, const std::vector<std::string>& names);

}  // namespace mythos
