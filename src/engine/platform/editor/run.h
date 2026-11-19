#ifndef MYTHOS_ENGINE_PLATFORM_EDITOR_RUN_H_
#define MYTHOS_ENGINE_PLATFORM_EDITOR_RUN_H_
// Runs the level editor: opens the window, runs the frame loop, returns the
// process's exit code. A game's main() (the file that includes
// <SDL3/SDL_main.h>) calls it with its Adapter.

#include <string>

#include "engine/editor/adapter.h"

namespace mythos::editor {

struct EditorConfig {
  const Adapter* adapter = nullptr;  // null: levels, furniture and decor only
  std::string project_dir = ".";     // --project's default (holds assets/)
  std::string stage_dir;  // --assets's default; "" = <project_dir>/assets
};

// Reads the command line (see run.cpp) and runs until the window closes.
int RunEditor(int argc, char** argv, const EditorConfig& config);

}  // namespace mythos::editor

#endif  // MYTHOS_ENGINE_PLATFORM_EDITOR_RUN_H_
