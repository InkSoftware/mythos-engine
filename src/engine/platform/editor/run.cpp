

#include "engine/platform/editor/run.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <numbers>
#include <string>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include "engine/core/fileio.h"
#include "engine/platform/editor/editor_app.h"
#include "engine/platform/gl/gl_api.h"
#include "engine/platform/window.h"
#include "imgui.h"

// The editor's window, ImGui setup and frame loop.
namespace {
// Whole-string number parse; `fallback` when the text is not a number.
int ParseInt(const std::string& text, const int fallback = 0) {
  char* end = nullptr;
  const int64_t v = std::strtoll(text.c_str(), &end, 10);
  return end != text.c_str() && *end == '\0' ? static_cast<int>(v) : fallback;
}

float ParseFloat(const std::string& text, const float fallback = 0.0f) {
  char* end = nullptr;
  const float v = std::strtof(text.c_str(), &end);
  return end != text.c_str() && *end == '\0' ? v : fallback;
}

// First installed system UI font from a short per-OS list, or null (ImGui's
// built-in font is then used). No font is bundled.
const char* FindUiFont() {
  static const char* kFonts[] = {
      "/System/Library/Fonts/SFNS.ttf",
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      "/Library/Fonts/Arial.ttf",
      R"(C:\Windows\Fonts\segoeui.ttf)",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
  };
  for (const char* f : kFonts)
    if (std::filesystem::exists(f)) return f;
  return nullptr;
}

// A project folder is one that holds assets/levels.
bool HasProject(const std::string& dir) {
  std::error_code ec;
  return !dir.empty() &&
         std::filesystem::is_directory(
             std::filesystem::path(dir) / "assets" / "levels", ec);
}

// The assets/ folder shipped beside the editor (inside an app bundle, next to
// the executable, or in a build tree), recognized by its models/ folder;
// empty if none.
std::string BundledAssets() {
  namespace fs = std::filesystem;
  // A development build has no assets beside it: MYTHOS_ENGINE_ASSET_DIR names
  // the engine's assets folder instead.
  if (const char* env = SDL_getenv("MYTHOS_ENGINE_ASSET_DIR")) {
    std::error_code ec;
    if (fs::is_directory(fs::path(env) / "models", ec)) return env;
  }
  if (const char* base = SDL_GetBasePath()) {
    for (const char* rel : {"assets", "../../../assets", "../assets"}) {
      const fs::path p = (fs::path(base) / rel).lexically_normal();
      std::error_code ec;
      if (fs::is_directory(p / "models", ec)) return p.string();
    }
  }
  return "";
}

// State shared with the asynchronous folder dialog callback.
struct PickState {
  bool done = false;
  std::string path;
};

// Folder dialog callback; may run on another thread. An empty path means the
// dialog was canceled.
void SDLCALL OnPicked(void* const userdata, const char* const* const list,
                      int) {
  auto* st = static_cast<PickState*>(userdata);
  if (list && list[0]) st->path = list[0];
  st->done = true;
}

// Asks for a project folder until one is valid. Returns empty if canceled.
// The dialog is asynchronous, so events are pumped while waiting to keep the
// window responsive.
std::string PickProject(SDL_Window* const window) {
  for (;;) {
    PickState st;
    SDL_ShowOpenFolderDialog(OnPicked, &st, window, nullptr, false);
    while (!st.done) {
      SDL_Event e;
      while (SDL_PollEvent(&e)) {
      }
      SDL_Delay(20);
    }
    if (st.path.empty() || HasProject(st.path)) return st.path;
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_WARNING, "Level editor",
        "That folder has no assets/levels. Pick the folder of the game that "
        "holds assets/.",
        window);
  }
}
}

namespace mythos::editor {
// Project resolution order: --project, then the folder remembered in
// project.txt, then a folder dialog. Screenshot mode (--shot) skips the
// dialog, the remembered folder and the layout file, renders --frames frames
// and writes a PPM of the window.
int RunEditor(const int argc, char** const argv, const EditorConfig& config) {
  EditorArgs args;
  args.project_dir = config.project_dir;
  args.stage_dir = config.stage_dir;
  std::string shot;
  int width = 1680, height = 1000, frames = 45;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto more = [&](const int n) { return i + n < argc; };
    if (a == "--project" && more(1))
      args.project_dir = argv[++i];
    else if (a == "--assets" && more(1))
      args.stage_dir = argv[++i];
    else if (a == "--game" && more(1))
      args.game_exe = argv[++i];
    else if (a == "--level" && more(1))
      args.level = argv[++i];
    else if (a == "--shot" && more(1))
      shot = argv[++i];
    else if (a == "--size" && more(2)) {
      width = ParseInt(argv[++i], width);
      height = ParseInt(argv[++i], height);
    } else if (a == "--frames" && more(1))
      frames = ParseInt(argv[++i], frames);
    else if (a == "--select" && more(1))
      args.select = argv[++i];
    else if (a == "--tool" && more(1))
      args.tool = argv[++i];
    else if (a == "--top")
      args.top = true;
    else if (a == "--preview")
      args.preview = true;
    else if (a == "--camera" && more(3)) {
      args.has_camera = true;
      args.heading = ParseFloat(argv[++i]) * std::numbers::pi_v<float> / 180.0f;
      args.pitch = ParseFloat(argv[++i]) * std::numbers::pi_v<float> / 180.0f;
      args.distance = ParseFloat(argv[++i]);
    }
  }

  const Adapter default_adapter;
  const Adapter* adapter = config.adapter ? config.adapter : &default_adapter;

  mythos::Window window;
  if (!window.Open((adapter->Name() + " - Level Editor").c_str(), width, height,
                   shot.empty()))
    return 1;

  const std::string last_file = mythos::PrefFilePath(
      adapter->PrefsOrg(), adapter->PrefsApp(), "project.txt");
  if (!HasProject(args.project_dir) && shot.empty()) {
    std::string last = mythos::ReadTextFile(last_file);
    while (!last.empty() && (last.back() == '\n' || last.back() == '\r'))
      last.pop_back();
    args.project_dir = HasProject(last) ? last : PickProject(window.Sdl());
    if (args.project_dir.empty()) return 0;
  }
  if (args.stage_dir.empty()) {
    const std::string own = args.project_dir + "/assets";
    const std::string bundled = BundledAssets();
    std::error_code ec;
    args.stage_dir =
        !bundled.empty() && !std::filesystem::is_directory(own + "/models", ec)
            ? bundled
            : own;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
  static std::string ini =
      shot.empty() ? mythos::PrefFilePath(adapter->PrefsOrg(),
                                          adapter->PrefsApp(), "layout.ini")
                   : "";
  io.IniFilename = ini.empty() ? nullptr : ini.c_str();
  float scale =
      SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window.Sdl()));
  if (scale < 1.0f || scale > 3.0f) scale = 1.0f;
  ApplyEditorTheme(scale);
  ImGui::GetStyle().FontScaleDpi = scale;
  if (const char* font = FindUiFont())
    io.Fonts->AddFontFromFileTTF(font, 15.0f);
  else
    io.Fonts->AddFontDefault();
  ImGui_ImplSDL3_InitForOpenGL(window.Sdl(), SDL_GL_GetCurrentContext());
  // GLSL 150 is the oldest version the ImGui backend needs and works on a
  // 4.1 core context.
  ImGui_ImplOpenGL3_Init("#version 150");

  int exit_code = 0;
  {
    EditorApp app;
    std::string error;
    if (!app.Init(args, adapter, &error)) {
      SDL_Log("cannot start the editor: %s", error.c_str());
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Level editor",
                               ("Cannot start the editor: " + error).c_str(),
                               window.Sdl());
      exit_code = 1;
    } else {
      if (shot.empty()) mythos::WriteFileAtomic(last_file, args.project_dir);
      Uint64 last = SDL_GetPerformanceCounter();
      int settle = 0;
      for (int frame = 0; !app.WantsQuit(); ++frame) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
          ImGui_ImplSDL3_ProcessEvent(&e);
          if (e.type == SDL_EVENT_QUIT ||
              (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
               e.window.windowID == SDL_GetWindowID(window.Sdl())))
            app.RequestQuit();
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        const float dt = static_cast<float>(now - last) /
                         static_cast<float>(SDL_GetPerformanceFrequency());
        last = now;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        app.Frame(dt);
        ImGui::Render();
        int w, h;
        window.DrawableSize(w, h);
        gl::BindFramebuffer(gl::kFramebuffer, 0);
        gl::Viewport(0, 0, w, h);
        gl::ClearColor(0.07f, 0.075f, 0.085f, 1);
        gl::Clear(gl::kColorBufferBit | gl::kDepthBufferBit);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (args.preview && app.HasPreview()) ++settle;
        if (!shot.empty() && frame + 1 >= frames && !app.Busy() &&
            (!args.preview || settle > 8 || frame > 20000)) {
          if (!mythos::WriteFramebufferPpm(shot, w, h)) exit_code = 1;
          break;
        }
        window.Swap();
        if (frame % 30 == 0) window.SetTitle(app.WindowTitle());
      }
    }
  }
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  return exit_code;
}
}
