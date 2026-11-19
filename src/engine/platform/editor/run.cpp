// The editor's window, Dear ImGui backends and frame loop. A game's main()
// calls RunEditor() with its Adapter; the editor itself is editor_app.cpp.
//
//   <editor> [--project DIR] [--assets DIR] [--game EXE] [--level NAME]
//       --project DIR   the game's repository (holds assets/)
//       --assets DIR    the merged assets (textures, models; the build's asset
//                       stage); default: the project's assets
//       --game EXE      the game, for the preview (default: next to the editor)
//       --level NAME    open this level first
//   Screenshots (render a few frames, save the window as a PPM, exit):
//       --shot FILE [--size W H] [--frames N] [--camera HEADING_DEG PITCH_DEG
//       DISTANCE] [--top] [--select furniture:N|decor:N|NAME] [--tool
//       select|move|rotate|paint|place] [--preview]

#include "engine/platform/editor/run.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include "engine/platform/editor/editor_app.h"
#include "engine/platform/gl/gl_api.h"
#include "engine/platform/window.h"
#include "imgui.h"

namespace {

const char* FindUiFont() {
  static const char* kFonts[] = {
      "/System/Library/Fonts/SFNS.ttf",
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      "/Library/Fonts/Arial.ttf",
      "C:\\Windows\\Fonts\\segoeui.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
  };
  for (const char* f : kFonts)
    if (std::filesystem::exists(f)) return f;
  return nullptr;
}

}  // namespace

namespace mythos::editor {

int RunEditor(int argc, char** argv, const EditorConfig& config) {
  EditorArgs args;
  args.project_dir = config.project_dir;
  args.stage_dir = config.stage_dir;
  std::string shot;
  int width = 1680, height = 1000, frames = 45;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto more = [&](int n) { return i + n < argc; };
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
      width = std::atoi(argv[++i]);
      height = std::atoi(argv[++i]);
    } else if (a == "--frames" && more(1))
      frames = std::atoi(argv[++i]);
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
      args.heading =
          static_cast<float>(std::atof(argv[++i])) * 3.14159265f / 180.0f;
      args.pitch =
          static_cast<float>(std::atof(argv[++i])) * 3.14159265f / 180.0f;
      args.distance = static_cast<float>(std::atof(argv[++i]));
    }
  }

  if (args.stage_dir.empty()) args.stage_dir = args.project_dir + "/assets";
  const Adapter default_adapter;
  const Adapter* adapter = config.adapter ? config.adapter : &default_adapter;

  mythos::Window window;
  if (!window.Open((adapter->Name() + " - Level Editor").c_str(), width, height,
                   shot.empty()))
    return 1;

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
  ImGui_ImplOpenGL3_Init("#version 150");

  int exit_code = 0;
  {
    EditorApp app;
    std::string error;
    if (!app.Init(args, adapter, &error)) {
      SDL_Log("cannot start the editor: %s", error.c_str());
      exit_code = 1;
    } else {
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
        if (args.preview && app.HasPreview())
          ++settle;  // let the Game Preview tab come up
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

}  // namespace mythos::editor
