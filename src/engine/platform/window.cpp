#include "engine/platform/window.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "engine/platform/gl/gl_api.h"

namespace mythos {
namespace {
// Smallest window the overlay layout stays usable at.
constexpr int kMinWindowWidth = 640;
constexpr int kMinWindowHeight = 400;
constexpr int kDepthBits = 24;
constexpr int kGlMajor = 4, kGlMinor = 1;
}

bool Window::Open(const std::string& title, const int width, const int height,
                  const bool vsync) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return false;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, kGlMajor);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, kGlMinor);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  // Forward-compatible is required for a core context on macOS.
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
                      SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, kDepthBits);

  title_ = title;
  window_ = SDL_CreateWindow(
      title.c_str(), width, height,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window_) {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    Close();
    return false;
  }
  SDL_SetWindowMinimumSize(window_, kMinWindowWidth, kMinWindowHeight);
  context_ = SDL_GL_CreateContext(window_);
  if (!context_) {
    SDL_Log("OpenGL 4.1 core context unavailable: %s", SDL_GetError());
    Close();
    return false;
  }
  SDL_GL_SetSwapInterval(vsync ? 1 : 0);
  vsync_ = vsync ? 1 : 0;
  if (!gl::LoadFunctions()) {
    SDL_Log("could not load OpenGL functions");
    Close();
    return false;
  }
  return true;
}

void Window::SetVsync(const bool on) {
  if (!context_ || vsync_ == (on ? 1 : 0)) return;
  SDL_GL_SetSwapInterval(on ? 1 : 0);
  vsync_ = on ? 1 : 0;
}

void Window::Close() {
  if (context_) {
    SDL_GL_DestroyContext(context_);
    gl::UnloadFunctions();
  }
  if (window_) SDL_DestroyWindow(window_);
  if (context_ || window_ || SDL_WasInit(SDL_INIT_VIDEO)) SDL_Quit();
  context_ = nullptr;
  window_ = nullptr;
}

void Window::ApplyMode(const bool fullscreen, const int w, const int h) {
  if (!window_) return;
  if (mode_applied_ && fullscreen == fullscreen_ &&
      (fullscreen || (w == mode_w_ && h == mode_h_))) {
    return;
  }
  const bool was_fullscreen = mode_applied_ && fullscreen_;
  mode_applied_ = true;
  fullscreen_ = fullscreen;
  mode_w_ = w;
  mode_h_ = h;
  if (fullscreen) {
    SDL_SetWindowFullscreen(window_, true);
    return;
  }
  if (was_fullscreen) SDL_SetWindowFullscreen(window_, false);
  SDL_SyncWindow(window_);
  SDL_SetWindowSize(window_, w, h);
  SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED,
                        SDL_WINDOWPOS_CENTERED);
}

void Window::UsableDisplaySize(int& w, int& h) const {
  w = h = 0;
  SDL_Rect bounds{};
  SDL_DisplayID display = window_ ? SDL_GetDisplayForWindow(window_) : 0;
  if (display == 0) display = SDL_GetPrimaryDisplay();
  if (SDL_GetDisplayUsableBounds(display, &bounds)) {
    w = bounds.w;
    h = bounds.h;
  }
}

void Window::SetTitle(const std::string& title) {
  if (!window_ || title == title_) return;
  title_ = title;
  SDL_SetWindowTitle(window_, title.c_str());
}

void Window::DrawableSize(int& w, int& h) const {
  w = h = 0;
  if (window_) SDL_GetWindowSizeInPixels(window_, &w, &h);
}

void Window::Swap() {
  if (window_) SDL_GL_SwapWindow(window_);
}

bool WriteFramebufferPpm(const std::string& path, const int w, const int h) {
  constexpr size_t kRgbaBytes = 4, kRgbBytes = 3;
  std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * kRgbaBytes);
  gl::ReadPixels(0, 0, w, h, gl::kRgba, gl::kUnsignedByte, rgba.data());
  std::string ppm =
      "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
  ppm.reserve(ppm.size() + static_cast<size_t>(w) * h * kRgbBytes);
  // GL reads bottom-up and PPM is top-down; also drops the alpha channel.
  for (int row = h - 1; row >= 0; --row) {
    for (int col = 0; col < w; ++col) {
      const uint8_t* pixel =
          &rgba[(static_cast<size_t>(row) * w + col) * kRgbaBytes];
      ppm.append(reinterpret_cast<const char*>(pixel), kRgbBytes);
    }
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out.write(ppm.data(), static_cast<std::streamsize>(ppm.size()));
  return static_cast<bool>(out);
}

std::string PrefFilePath(const std::string& org, const std::string& app,
                         const std::string& file) {
  if (char* pref_dir = SDL_GetPrefPath(org.c_str(), app.c_str())) {
    const std::string dir = pref_dir;
    SDL_free(pref_dir);
    return dir + file;
  }
  return file;
}

std::map<std::string, std::string> ReadKeyValueFile(const std::string& path) {
  std::map<std::string, std::string> out;
  std::ifstream file(path);
  std::string line;
  while (std::getline(file, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    out[line.substr(0, eq)] = line.substr(eq + 1);
  }
  return out;
}

bool WriteKeyValueFile(const std::string& path,
                       const std::map<std::string, std::string>& values) {
  std::ofstream f(path);
  if (!f) return false;
  for (const auto& [key, value] : values) f << key << '=' << value << '\n';
  return static_cast<bool>(f);
}

std::string FindAssetDir(const std::string& probe) {
  namespace fs = std::filesystem;
  if (const char* base = SDL_GetBasePath()) {
    const std::string dir = base;
    for (const char* rel : {"assets", "../Resources/assets"}) {
      const fs::path candidate = fs::path(dir) / rel;
      if (fs::exists(candidate / probe)) return candidate.string();
    }
  }
  return "assets";
}
}
