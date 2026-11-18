#include "engine/platform/app.h"

#include "engine/core/fileio.h"

namespace mythos {

std::string UserFiles::read(const std::string& file) const {
    const std::string fullPath = path(file);
    return fullPath.empty() ? std::string() : readTextFile(fullPath);
}

bool UserFiles::write(const std::string& file, const std::string& text, bool removeIfEmpty) const {
    const std::string fullPath = path(file);
    if (fullPath.empty()) return false;
    if (writeFileAtomic(fullPath, text, removeIfEmpty)) return true;
    SDL_Log("cannot write %s", fullPath.c_str());
    return false;
}

MousePointer::MousePointer()
    : arrow_(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT)), hand_(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER)) {}

MousePointer::~MousePointer() {
    if (arrow_) SDL_DestroyCursor(arrow_);
    if (hand_) SDL_DestroyCursor(hand_);
}

int MousePointer::itemAt(const Window& window, int overlayW, int overlayH, const std::vector<Hotspot>& hotspots,
                         int mx, int my, int* value) const {
    int windowW = 0, windowH = 0;
    SDL_GetWindowSize(window.sdl(), &windowW, &windowH);
    if (windowW <= 0 || windowH <= 0) return -1;
    // Window pixels to overlay pixels.
    const float overlayX = static_cast<float>(mx) * static_cast<float>(overlayW) / static_cast<float>(windowW);
    const float overlayY = static_cast<float>(my) * static_cast<float>(overlayH) / static_cast<float>(windowH);
    return hitTest(hotspots, overlayX, overlayY, value);
}

void MousePointer::updateCursor(const Window& window, int overlayW, int overlayH, const std::vector<Hotspot>& hotspots) const {
    float mouseX = 0, mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const bool overItem = itemAt(window, overlayW, overlayH, hotspots, static_cast<int>(mouseX), static_cast<int>(mouseY)) >= 0;
    SDL_Cursor* wanted = overItem ? hand_ : arrow_;
    if (SDL_GetCursor() != wanted) SDL_SetCursor(wanted);
}

void playCue(AudioEngine& audio, const SoundCue& cue, Rng& rng) {
    if (cue.clips.empty()) return;
    const CuePick pick = pickCue(cue, rng);
    audio.play(cue.clips[pick.clip], cue.gain, pick.pitch);
}

std::map<std::string, double> clipLengths(const AudioEngine& audio, const std::vector<std::string>& names) {
    std::map<std::string, double> out;
    for (const std::string& name : names)
        if (const double sec = audio.clipSeconds(name); sec > 0) out[name] = sec;
    return out;
}

}  // namespace mythos
