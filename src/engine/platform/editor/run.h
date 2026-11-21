#ifndef MYTHOS_ENGINE_PLATFORM_EDITOR_RUN_H_
#define MYTHOS_ENGINE_PLATFORM_EDITOR_RUN_H_

#include <string>

#include "engine/editor/adapter.h"

namespace mythos::editor {
// What a game (or the generic editor) passes to RunEditor.
struct EditorConfig {
  // The game's adapter; null selects the generic one. Not owned.
  const Adapter* adapter = nullptr;
  // Default project folder (one holding assets/levels); the command line,
  // the remembered folder and a folder dialog can override it.
  std::string project_dir = ".";
  // Default folder of the textures and models, empty to derive it.
  std::string stage_dir;
};

// Opens the editor window and runs it until it is closed. Command-line flags:
// --project, --assets, --game, --level, --select, --tool, --top, --camera,
// --preview, and for scripted screenshots --shot, --size and --frames.
// Returns the process exit code (0 on a clean exit).
int RunEditor(int argc, char** argv, const EditorConfig& config);
}

#endif
