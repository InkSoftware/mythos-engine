#include "engine/platform/window.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "engine/platform/gl/gl_api.h"

namespace mythos {
namespace {

constexpr int kMinWindowWidth = 640;   // below this the menus no longer fit
constexpr int kMinWindowHeight = 400;
constexpr int kDepthBits = 24;
constexpr int kGlMajor = 4, kGlMinor = 1;  // the ceiling on macOS (see CLAUDE.md)

}  // namespace

bool Window::open(const std::string& title, int width, int height, bool vsync) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, kGlMajor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, kGlMinor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);  // required on macOS
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, kDepthBits);

    title_ = title;
    window_ = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        close();
        return false;
    }
    SDL_SetWindowMinimumSize(window_, kMinWindowWidth, kMinWindowHeight);
    context_ = SDL_GL_CreateContext(window_);
    if (!context_) {
        SDL_Log("OpenGL 4.1 core context unavailable: %s", SDL_GetError());
        close();
        return false;
    }
    SDL_GL_SetSwapInterval(vsync ? 1 : 0);
    vsync_ = vsync ? 1 : 0;
    if (!gl::loadFunctions()) {
        SDL_Log("could not load OpenGL functions");
        close();
        return false;
    }
    return true;
}

void Window::setVsync(bool on) {
    if (!context_ || vsync_ == (on ? 1 : 0)) return;
    SDL_GL_SetSwapInterval(on ? 1 : 0);
    vsync_ = on ? 1 : 0;
}

void Window::close() {
    if (context_) {
        SDL_GL_DestroyContext(context_);
        gl::unloadFunctions();
    }
    if (window_) SDL_DestroyWindow(window_);
    if (context_ || window_ || SDL_WasInit(SDL_INIT_VIDEO)) SDL_Quit();
    context_ = nullptr;
    window_ = nullptr;
}

void Window::applyMode(bool fullscreen, int w, int h) {
    if (!window_) return;
    if (modeApplied_ && fullscreen == fullscreen_ && (fullscreen || (w == modeW_ && h == modeH_))) return;
    const bool wasFullscreen = modeApplied_ && fullscreen_;
    modeApplied_ = true;
    fullscreen_ = fullscreen;
    modeW_ = w;
    modeH_ = h;
    if (fullscreen) {
        SDL_SetWindowFullscreen(window_, true);  // borderless, at the desktop resolution
        return;
    }
    if (wasFullscreen) SDL_SetWindowFullscreen(window_, false);
    SDL_SyncWindow(window_);  // let the leave-fullscreen finish before resizing
    SDL_SetWindowSize(window_, w, h);
    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

void Window::usableDisplaySize(int& w, int& h) const {
    w = h = 0;
    SDL_Rect bounds{};
    SDL_DisplayID display = window_ ? SDL_GetDisplayForWindow(window_) : 0;
    if (display == 0) display = SDL_GetPrimaryDisplay();
    if (SDL_GetDisplayUsableBounds(display, &bounds)) {
        w = bounds.w;
        h = bounds.h;
    }
}

void Window::setTitle(const std::string& title) {
    if (!window_ || title == title_) return;
    title_ = title;
    SDL_SetWindowTitle(window_, title.c_str());
}

void Window::drawableSize(int& w, int& h) const {
    w = h = 0;
    if (window_) SDL_GetWindowSizeInPixels(window_, &w, &h);
}

void Window::swap() {
    if (window_) SDL_GL_SwapWindow(window_);
}

bool writeFramebufferPpm(const std::string& path, int w, int h) {
    constexpr size_t kRgbaBytes = 4, kRgbBytes = 3;
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * kRgbaBytes);
    gl::ReadPixels(0, 0, w, h, gl::RGBA, gl::UNSIGNED_BYTE, rgba.data());
    std::string ppm = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
    ppm.reserve(ppm.size() + static_cast<size_t>(w) * h * kRgbBytes);
    for (int row = h - 1; row >= 0; --row)  // GL rows start at the bottom
        for (int col = 0; col < w; ++col) {
            const uint8_t* pixel = &rgba[(static_cast<size_t>(row) * w + col) * kRgbaBytes];
            ppm.append(reinterpret_cast<const char*>(pixel), kRgbBytes);  // drop alpha
        }
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(ppm.data(), static_cast<std::streamsize>(ppm.size()));
    return static_cast<bool>(out);
}

std::string prefFilePath(const std::string& org, const std::string& app, const std::string& file) {
    if (char* prefDir = SDL_GetPrefPath(org.c_str(), app.c_str())) {
        const std::string dir = prefDir;
        SDL_free(prefDir);
        return dir + file;
    }
    return file;
}

std::map<std::string, std::string> readKeyValueFile(const std::string& path) {
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

bool writeKeyValueFile(const std::string& path, const std::map<std::string, std::string>& values) {
    std::ofstream f(path);
    if (!f) return false;
    for (const auto& [key, value] : values) f << key << '=' << value << '\n';
    return static_cast<bool>(f);
}

std::string findAssetDir(const std::string& probe) {
    namespace fs = std::filesystem;
    if (const char* base = SDL_GetBasePath()) {  // directory of the executable, with a trailing separator
        const std::string dir = base;  // owned by SDL, not freed
        for (const char* rel : {"assets", "../Resources/assets"}) {
            const fs::path candidate = fs::path(dir) / rel;
            if (fs::exists(candidate / probe)) return candidate.string();
        }
    }
    return "assets";
}

}  // namespace mythos
