#ifndef MYTHOS_ENGINE_GFX_MODEL_H_
#define MYTHOS_ENGINE_GFX_MODEL_H_
// Loads 3D objects from Wavefront OBJ files (assets/models/*.obj).
//
// Supported subset: v, vt, vn, usemtl, f (triangles and quads, "a", "a/b",
// "a//c", "a/b/c"). Each usemtl name is a game texture name. A name starting
// with "glow_" is drawn at full brightness with the texture named after the
// prefix (e.g. "glow_computer" = the computer texture, emissive). Models are
// local: feet at the origin, front facing +z.

#include <map>
#include <string>
#include <vector>

#include "engine/gfx/mesh.h"

namespace mythos {

struct Model {
  std::string name;
  std::vector<Batch> batches;
};

// Parses OBJ text into batches (one per material). Returns false and fills
// *error on bad input.
bool ParseObj(const std::string& text, std::vector<Batch>& out,
              std::string* error = nullptr);

class ModelLibrary {
 public:
  // Loads every .obj file in dir. The name of a model is its file name without
  // extension.
  bool LoadDir(const std::string& dir, std::string* error = nullptr);
  const Model* Find(const std::string& name) const;
  size_t Size() const { return models_.size(); }

 private:
  std::map<std::string, Model> models_;
};

}  // namespace mythos

#endif  // MYTHOS_ENGINE_GFX_MODEL_H_
