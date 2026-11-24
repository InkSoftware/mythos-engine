#ifndef MYTHOS_ENGINE_GFX_MODEL_H_
#define MYTHOS_ENGINE_GFX_MODEL_H_

#include <map>
#include <string>
#include <vector>

#include "engine/gfx/mesh.h"

// Loads 3D objects from Wavefront OBJ files (assets/models/*.obj).
//
// Supported subset: v, vt, vn, usemtl and f (triangles and larger convex
// polygons, with indices "a", "a/b", "a//c" or "a/b/c"; negative indices count
// from the end). Each usemtl name is a game texture name; a name starting with
// "glow_" is drawn at full brightness (emissive) with the texture named after
// the prefix. Models are local: feet at the origin, front facing +z.
namespace mythos {
// A named model: one batch per material.
struct Model {
  std::string name;
  std::vector<Batch> batches;
};

// Parses OBJ `text` into `out` (one batch per material). Returns false on a
// malformed line and, when `error` is not null, describes it there. Missing
// normals default to +y, missing texture coordinates to (0, 0), and faces
// before any usemtl use the texture "missing".
bool ParseObj(const std::string& text, std::vector<Batch>& out,
              std::string* error = nullptr);

// The models of an asset folder, looked up by name.
class ModelLibrary {
 public:
  // Loads every .obj file in `dir` (through the vfs); a model's name is its
  // file name without extension, and a later load replaces a model of the same
  // name. Returns false, with the reason in `error` when not null, if the
  // folder is missing or empty, or a file cannot be read or parsed.
  bool LoadDir(const std::string& dir, std::string* error = nullptr);

  // Loads one .obj file (through the vfs), replacing the model of its name; a
  // file that cannot be read or parsed leaves the old model in place. For
  // reloading a model that changed on disk. Returns false with the reason in
  // `error` when not null.
  bool LoadFile(const std::string& path, std::string* error = nullptr);

  // The model called `name`, or null. The pointer stays valid until the library
  // is destroyed or reloaded.
  [[nodiscard]] const Model* Find(const std::string& name) const;

  [[nodiscard]] size_t Size() const { return models_.size(); }

 private:
  std::map<std::string, Model> models_;
};
}

#endif
