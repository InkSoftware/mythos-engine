#include "engine/editor/adapter.h"

#include <filesystem>

#include "engine/core/vfs.h"
#include "engine/gfx/materials.h"

namespace mythos::editor {

void Adapter::SetupTextures(TextureLibrary& lib,
                            const std::string& stage_dir) const {
  AddCommonMaterials(lib);
  lib.LoadOverrides(stage_dir);
  RegisterModelTextures(lib, stage_dir + "/modeltex");
}

int RegisterModelTextures(TextureLibrary& lib, const std::string& dir) {
  int registered = 0;
  for (const std::string& file : vfs::List(dir)) {
    const std::filesystem::path entry(file);
    if (entry.extension() != ".bmp") continue;
    const std::string name = entry.stem().string();
    if (lib.Has(name)) continue;
    // A stand-in until the file loads.
    lib.Add(name, Surface(1, 1, Rgba(128, 128, 128)), true);
    ++registered;
  }
  lib.LoadOverrides(dir);
  return registered;
}

}  // namespace mythos::editor
