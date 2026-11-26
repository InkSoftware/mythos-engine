

#include "engine/platform/editor/run.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <numbers>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

namespace {
// A scripted session for checking the editor without a person: `--script
// file` runs commands against the hidden window, one line each:
//   frames N        run N more frames (the UI settles)
//   panel NAME      show a panel (graph, cutscene, inspector, problems)
//   mouse X Y       move the pointer (window pixels)
//   click           press and release the left button (two frames)
//   down / up       hold or release the left button
//   drag X1 Y1 X2 Y2   press at 1, move in steps, release at 2
//   scroll N        wheel notches at the pointer (negative scrolls down)
//   key NAME        press a key (Delete, Escape, ...)
//   shift on|off    hold Shift (alt likewise)
//   shot PATH       write the window as a PPM
//   print           print the open level's summary line
//   expect TEXT     fail (exit code 1) unless the summary contains TEXT
// `#` starts a comment.
struct Script {
  struct Step {
    std::string op;
    std::vector<std::string> args;
  };
  std::vector<Step> steps;
  size_t next = 0;
  int wait = 0;                            // frames left before the next step
  std::vector<std::pair<int, int>> queue;  // pending mouse moves for a drag
  bool done() const {
    return next >= steps.size() && queue.empty() && wait <= 0;
  }

  bool Load(const std::string& text) {
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
      if (const size_t hash = line.find('#'); hash != std::string::npos)
        line.resize(hash);
      std::istringstream ls(line);
      Step st;
      if (!(ls >> st.op)) continue;
      // `expect` keeps the rest of the line as one argument.
      if (st.op == "expect") {
        std::string rest;
        std::getline(ls, rest);
        const size_t a = rest.find_first_not_of(' ');
        st.args.push_back(a == std::string::npos ? "" : rest.substr(a));
      } else {
        for (std::string arg; ls >> arg;) st.args.push_back(arg);
      }
      steps.push_back(std::move(st));
    }
    return !steps.empty();
  }
};

ImGuiKey KeyByName(const std::string& n) {
  if (n == "Delete") return ImGuiKey_Delete;
  if (n == "Escape") return ImGuiKey_Escape;
  if (n == "Enter") return ImGuiKey_Enter;
  if (n == "Tab") return ImGuiKey_Tab;
  return ImGuiKey_None;
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
  std::string script_path;
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
    else if (a == "--script" && more(1))
      script_path = argv[++i];
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
  // A scripted session is hidden and keeps no layout file, like --shot.
  const bool scripted = !script_path.empty();
  if (!window.Open((adapter->Name() + " - Level Editor").c_str(), width, height,
                   shot.empty() && !scripted))
    return 1;

  const std::string last_file = mythos::PrefFilePath(
      adapter->PrefsOrg(), adapter->PrefsApp(), "project.txt");
  if (!HasProject(args.project_dir) && shot.empty() && !scripted) {
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
      shot.empty() && !scripted
          ? mythos::PrefFilePath(adapter->PrefsOrg(), adapter->PrefsApp(),
                                 "layout.ini")
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
      if (shot.empty() && !scripted)
        mythos::WriteFileAtomic(last_file, args.project_dir);
      Script script;
      if (scripted && !script.Load(mythos::ReadTextFile(script_path))) {
        SDL_Log("cannot read the script %s", script_path.c_str());
        exit_code = 1;
      }
      float mouse_x = 0, mouse_y = 0;
      Uint64 last = SDL_GetPerformanceCounter();
      int settle = 0;
      for (int frame = 0;
           !app.WantsQuit() && exit_code == 0 && !(scripted && script.done());
           ++frame) {
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
        if (scripted) {
          ImGuiIO& sio = ImGui::GetIO();
          if (!script.queue.empty()) {  // the steps of a drag
            mouse_x = static_cast<float>(script.queue.front().first);
            mouse_y = static_cast<float>(script.queue.front().second);
            script.queue.erase(script.queue.begin());
            sio.AddMousePosEvent(mouse_x, mouse_y);
          } else if (script.wait > 0) {
            --script.wait;
          }
          while (script.queue.empty() && script.wait <= 0 &&
                 script.next < script.steps.size() && exit_code == 0) {
            const Script::Step& st = script.steps[script.next++];
            const auto num = [&](const size_t i) {
              return i < st.args.size() ? ParseInt(st.args[i].c_str(), 0) : 0;
            };
            if (st.op == "frames") {
              script.wait = std::max(1, num(0));
            } else if (st.op == "panel" && !st.args.empty()) {
              app.ShowPanel(st.args[0]);
            } else if (st.op == "mouse") {
              mouse_x = static_cast<float>(num(0));
              mouse_y = static_cast<float>(num(1));
              sio.AddMousePosEvent(mouse_x, mouse_y);
              script.wait = 1;
            } else if (st.op == "click") {
              sio.AddMouseButtonEvent(0, true);
              script.steps.insert(script.steps.begin() +
                                      static_cast<std::ptrdiff_t>(script.next),
                                  {"up", {}});
              script.wait = 2;
            } else if (st.op == "down" || st.op == "up") {
              sio.AddMouseButtonEvent(0, st.op == "down");
              script.wait = 2;
            } else if (st.op == "drag") {
              sio.AddMousePosEvent(static_cast<float>(num(0)),
                                   static_cast<float>(num(1)));
              sio.AddMouseButtonEvent(0, true);
              constexpr int kSteps = 8;
              for (int k = 1; k <= kSteps; ++k) {
                script.queue.emplace_back(
                    num(0) + (num(2) - num(0)) * k / kSteps,
                    num(1) + (num(3) - num(1)) * k / kSteps);
              }
              script.steps.insert(script.steps.begin() +
                                      static_cast<std::ptrdiff_t>(script.next),
                                  {"up", {}});
              script.wait = 2;
            } else if (st.op == "scroll") {  // wheel notches at the pointer
              sio.AddMouseWheelEvent(0.0f, static_cast<float>(num(0)));
              script.wait = 2;
            } else if (st.op == "key" && !st.args.empty()) {
              const ImGuiKey key = KeyByName(st.args[0]);
              sio.AddKeyEvent(key, true);
              script.steps.insert(script.steps.begin() +
                                      static_cast<std::ptrdiff_t>(script.next),
                                  {"keyup", {st.args[0]}});
              script.wait = 2;
            } else if (st.op == "keyup" && !st.args.empty()) {
              sio.AddKeyEvent(KeyByName(st.args[0]), false);
              script.wait = 2;
            } else if (st.op == "shift" || st.op == "alt") {
              const bool on = !st.args.empty() && st.args[0] == "on";
              sio.AddKeyEvent(st.op == "shift" ? ImGuiMod_Shift : ImGuiMod_Alt,
                              on);
              script.wait = 1;
            } else if (st.op == "shot" && !st.args.empty()) {
              int sw, sh;
              window.DrawableSize(sw, sh);
              gl::BindFramebuffer(gl::kFramebuffer, 0);
              if (!mythos::WriteFramebufferPpm(st.args[0], sw, sh))
                exit_code = 1;
            } else if (st.op == "print") {
              std::printf("%s\n", app.Summary().c_str());
            } else if (st.op == "expect") {
              const std::string summary = app.Summary();
              const bool ok = !st.args.empty() &&
                              summary.find(st.args[0]) != std::string::npos;
              std::printf("%s: %s\n", ok ? "ok" : "FAILED",
                          st.args.empty() ? "" : st.args[0].c_str());
              if (!ok) {
                std::printf("  summary: %s\n", summary.c_str());
                exit_code = 1;
              }
            } else {
              std::printf("unknown script step: %s\n", st.op.c_str());
              exit_code = 1;
            }
          }
        }
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
