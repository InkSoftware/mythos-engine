#include "engine/platform/app.h"

#include "engine/core/fileio.h"
#include "engine/core/vfs.h"

namespace mythos {

std::string UserFiles::Read(const std::string& file) const {
  const std::string full_path = Path(file);
  return full_path.empty() ? std::string() : ReadTextFile(full_path);
}

bool UserFiles::Write(const std::string& file, const std::string& text,
                      bool remove_if_empty) const {
  const std::string full_path = Path(file);
  if (full_path.empty()) return false;
  if (WriteFileAtomic(full_path, text, remove_if_empty)) return true;
  SDL_Log("cannot write %s", full_path.c_str());
  return false;
}

MousePointer::MousePointer()
    : arrow_(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT)),
      hand_(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER)) {}

MousePointer::~MousePointer() {
  if (arrow_) SDL_DestroyCursor(arrow_);
  if (hand_) SDL_DestroyCursor(hand_);
}

int MousePointer::ItemAt(const Window& window, int overlay_w, int overlay_h,
                         const std::vector<Hotspot>& hotspots, int mx, int my,
                         int* value) {
  int window_w = 0, window_h = 0;
  SDL_GetWindowSize(window.Sdl(), &window_w, &window_h);
  if (window_w <= 0 || window_h <= 0) return -1;
  // Window pixels to overlay pixels.
  const float overlay_x = static_cast<float>(mx) *
                          static_cast<float>(overlay_w) /
                          static_cast<float>(window_w);
  const float overlay_y = static_cast<float>(my) *
                          static_cast<float>(overlay_h) /
                          static_cast<float>(window_h);
  return HitTest(hotspots, overlay_x, overlay_y, value);
}

void MousePointer::UpdateCursor(const Window& window, int overlay_w,
                                int overlay_h,
                                const std::vector<Hotspot>& hotspots) const {
  float mouse_x = 0, mouse_y = 0;
  SDL_GetMouseState(&mouse_x, &mouse_y);
  const bool over_item =
      ItemAt(window, overlay_w, overlay_h, hotspots, static_cast<int>(mouse_x),
             static_cast<int>(mouse_y)) >= 0;
  SDL_Cursor* wanted = over_item ? hand_ : arrow_;
  if (SDL_GetCursor() != wanted) SDL_SetCursor(wanted);
}

void PlayCue(AudioEngine& audio, const SoundCue& cue, Rng& rng) {
  if (cue.clips.empty()) return;
  const CuePick pick = PickCue(cue, rng);
  audio.Play(cue.clips[pick.clip], cue.gain, pick.pitch);
}

std::map<std::string, double> ClipLengths(
    const AudioEngine& audio, const std::vector<std::string>& names) {
  std::map<std::string, double> out;
  for (const std::string& name : names) {
    if (const double sec = audio.ClipSeconds(name); sec > 0) out[name] = sec;
  }
  return out;
}

std::string MountAssetPack(const std::string& pack_key) {
  const char* base =
      SDL_GetBasePath();  // owned by SDL, with a trailing separator
  if (!base) return "";
  const std::string name = "assets.pak";
  for (const char* rel : {"assets.pak", "../Resources/assets.pak"}) {
    const std::string pak = std::string(base) + rel;
    if (!vfs::Exists(pak)) continue;
    const std::string root = pak.substr(0, pak.size() - name.size()) + "assets";
    std::string error;
    if (vfs::Mount(pak, root, vfs::KeyFromString(pack_key), &error))
      return root;
    SDL_Log("%s: %s", pak.c_str(), error.c_str());
  }
  return "";
}

}  // namespace mythos
