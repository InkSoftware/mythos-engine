#ifndef MYTHOS_ENGINE_PLATFORM_WINDOW_H_
#define MYTHOS_ENGINE_PLATFORM_WINDOW_H_

#include <SDL3/SDL.h>

#include <map>
#include <string>

namespace mythos {
// The SDL window with its OpenGL 4.1 core context. Closing it also unloads the
// GL entry points. Not copyable.
class Window {
 public:
  Window() = default;

  Window(const Window&) = delete;

  Window& operator=(const Window&) = delete;

  ~Window() { Close(); }

  // Initializes SDL video, creates a resizable high-DPI window and the context
  // and loads GL. Returns false (after logging and cleaning up) on any failure.
  bool Open(const std::string& title, int width, int height, bool vsync = true);

  // Destroys the context and window and shuts SDL down. Safe to call twice.
  void Close();

  // Switches between fullscreen and a centerd window of w x h. A no-op when the
  // mode is already applied (the size is ignored in fullscreen).
  void ApplyMode(bool fullscreen, int w, int h);

  // Size of the display's usable area (excluding bars and docks); 0 x 0 if
  // unknown.
  void UsableDisplaySize(int& w, int& h) const;

  // Sets the swap interval, skipping the call when unchanged.
  void SetVsync(bool on);

  void SetTitle(const std::string& title);

  // Framebuffer size in pixels (differs from window size on high-DPI).
  void DrawableSize(int& w, int& h) const;

  // Presents the back buffer.
  void Swap();

  [[nodiscard]] SDL_Window* Sdl() const { return window_; }

 private:
  SDL_Window* window_ = nullptr;
  SDL_GLContext context_ = nullptr;
  int vsync_ = -1;
  bool mode_applied_ = false;
  bool fullscreen_ = false;
  int mode_w_ = 0, mode_h_ = 0;
  std::string title_;
};

// Saves the w x h framebuffer to a binary PPM (P6), flipped to top-down.
// Returns false if the file cannot be written.
bool WriteFramebufferPpm(const std::string& path, int w, int h);

// Path of `file` inside the per-user preference folder, which SDL creates; the
// bare `file` name if there is none.
std::string PrefFilePath(const std::string& org, const std::string& app,
                         const std::string& file);

// Parses `key=value` lines; lines without `=` are ignored and the value is
// everything after the first `=`. A missing file gives an empty map.
std::map<std::string, std::string> ReadKeyValueFile(const std::string& path);

// Writes `key=value` lines. Returns false on failure.
bool WriteKeyValueFile(const std::string& path,
                       const std::map<std::string, std::string>& values);

// Finds the `assets` folder beside the executable (or in a macOS bundle's
// Resources) that contains `probe`; falls back to the relative "assets".
std::string FindAssetDir(const std::string& probe);
}

#endif
