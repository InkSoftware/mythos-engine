#include "engine/platform/app.h"

#include "engine/core/fileio.h"

namespace mythos {

std::string UserFiles::read(const std::string& file) const {
    const std::string p = path(file);
    return p.empty() ? std::string() : readTextFile(p);
}

bool UserFiles::write(const std::string& file, const std::string& text, bool removeIfEmpty) const {
    const std::string p = path(file);
    if (p.empty()) return false;
    if (writeFileAtomic(p, text, removeIfEmpty)) return true;
    SDL_Log("cannot write %s", p.c_str());
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
    int ww = 0, wh = 0;
    SDL_GetWindowSize(window.sdl(), &ww, &wh);
    if (ww <= 0 || wh <= 0) return -1;
    return hitTest(hotspots, static_cast<float>(mx) * static_cast<float>(overlayW) / static_cast<float>(ww),
                   static_cast<float>(my) * static_cast<float>(overlayH) / static_cast<float>(wh), value);
}

void MousePointer::updateCursor(const Window& window, int overlayW, int overlayH, const std::vector<Hotspot>& hotspots) const {
    float mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    SDL_Cursor* want = itemAt(window, overlayW, overlayH, hotspots, static_cast<int>(mx), static_cast<int>(my)) >= 0 ? hand_ : arrow_;
    if (SDL_GetCursor() != want) SDL_SetCursor(want);
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
