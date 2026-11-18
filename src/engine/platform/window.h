#ifndef MYTHOS_ENGINE_PLATFORM_WINDOW_H_
#define MYTHOS_ENGINE_PLATFORM_WINDOW_H_
// An SDL window with an OpenGL 4.1 core context, plus small platform helpers:
// framebuffer screenshots, the per-user preferences folder and key=value
// settings files.

#include <SDL3/SDL.h>

#include <map>
#include <string>

namespace mythos {

class Window {
 public:
  Window() = default;
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  ~Window() { Close(); }

  // Initialises SDL video, opens a resizable window and makes a GL 4.1 core
  // context current, then loads the GL entry points. Logs and returns false on
  // failure.
  bool Open(const std::string& title, int width, int height, bool vsync = true);
  void Close();

  // Fullscreen (at the desktop's resolution) or a centred w x h window. Only
  // acts when the request changes, so it is cheap to call every frame; resizing
  // by hand still works.
  void ApplyMode(bool fullscreen, int w, int h);
  // The part of the window's display not taken by the menu bar, dock or
  // taskbar.
  void UsableDisplaySize(int& w, int& h) const;
  // Waits for the display's refresh when on; only acts when the request
  // changes.
  void SetVsync(bool on);
  void SetTitle(const std::string& title);  // UTF-8; a no-op when unchanged
  void DrawableSize(int& w, int& h) const;
  void Swap();
  SDL_Window* Sdl() const { return window_; }

 private:
  SDL_Window* window_ = nullptr;
  SDL_GLContext context_ = nullptr;
  int vsync_ = -1;  // -1 unset, else 0 / 1 as last applied
  bool mode_applied_ = false;
  bool fullscreen_ = false;
  int mode_w_ = 0, mode_h_ = 0;
  std::string title_;
};

// Reads the current GL framebuffer (w x h) and writes it as a binary PPM.
bool WriteFramebufferPpm(const std::string& path, int w, int h);

// <the user's preferences folder for org/app>/<file>, or just <file> if there
// is none.
std::string PrefFilePath(const std::string& org, const std::string& app,
                         const std::string& file);

// key=value lines. Missing files read as empty; lines without '=' are skipped.
std::map<std::string, std::string> ReadKeyValueFile(const std::string& path);
bool WriteKeyValueFile(const std::string& path,
                       const std::map<std::string, std::string>& values);

// The game data folder next to the executable: <exe dir>/assets (Windows,
// Linux) or <bundle>/Contents/Resources/assets (macOS). It must contain `probe`
// (a relative path). Falls back to "assets" (running from the project).
std::string FindAssetDir(const std::string& probe);

}  // namespace mythos

#endif  // MYTHOS_ENGINE_PLATFORM_WINDOW_H_
