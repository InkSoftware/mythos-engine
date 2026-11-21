

#include <SDL3/SDL_main.h>

#include "engine/platform/editor/run.h"

// The stock level editor: no game adapter, so it edits levels and the common
// data only. A game ships its own `main` that passes its adapter to
// `RunEditor`.
int main(const int argc, char** const argv) {
  return mythos::editor::RunEditor(argc, argv, {});
}
