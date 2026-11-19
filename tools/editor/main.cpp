// MythosEditor: the level editor with no game attached. It edits the levels of
// a project (assets/levels, the tile legend assets/data/tiles.json), their
// furniture and decor, with a 3D viewport. A game that wants its own things in
// it (props, lights, a preview in the game) builds its own editor: derive from
// mythos::editor::Adapter and call RunEditor() from its main().
//
//   MythosEditor --project DIR [--assets DIR]

#include <SDL3/SDL_main.h>

#include "engine/platform/editor/run.h"

int main(int argc, char** argv) {
  return mythos::editor::RunEditor(argc, argv, {});
}
