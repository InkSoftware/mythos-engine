// Loads a skinned model and its animation clips from a glTF 2.0 (a binary .glb
// or a text .gltf):
// the first skinned mesh node becomes skinned Batches, its skin a Skeleton and
// every animation a Clip. Headless; the renderer uploads the result.
//
// Supported: a GLB, or a text glTF with external or base64 buffers (any
// number); TRS or matrix nodes; triangle primitives with or without indices;
// JOINTS_0 / WEIGHTS_0 (bytes, shorts or floats); translation, rotation and
// scale channels with LINEAR, STEP (read as linear) or CUBICSPLINE (the values,
// without tangents) samplers; sparse accessors. Not supported: morph targets,
// more than one skin, more than kMaxBones joints. The skeleton's inverse bind
// matrices are rebuilt from the bind pose, not read from the file.
#ifndef MYTHOS_ENGINE_GFX_GLTF_H_
#define MYTHOS_ENGINE_GFX_GLTF_H_

#include <array>
#include <string>
#include <vector>

#include "engine/gfx/mesh.h"
#include "engine/gfx/skeleton.h"

namespace mythos {
struct GltfModel {
  // Skinned batches, one per primitive; each batch's texture is its material's
  // name ("missing" without one), so a game registers a texture of that name.
  std::vector<Batch> batches;
  // The linear base colour (RGBA) of each batch's material, for a game that
  // makes a flat texture of it.
  std::vector<std::array<float, 4>> base_colors;
  Skeleton skeleton;
  std::vector<Clip> clips;

  // The clip called `name`, or nullptr.
  [[nodiscard]] const Clip* FindClip(const std::string& name) const;
};

// Parses the bytes of a .glb. Returns false with the reason in `error` (when
// not null); `out` is untouched then.
bool ParseGlb(const std::string& bytes, GltfModel& out,
              std::string* error = nullptr);

// A text `.gltf`: `json_text` with its buffers read from files next to it
// (`base_dir`, through the vfs) or from base64 data URIs. Images and textures
// are ignored. Errors as ParseGlb.
bool ParseGltf(const std::string& json_text, const std::string& base_dir,
               GltfModel& out, std::string* error = nullptr);

// Reads a `.gltf` through the vfs (its buffers beside it) and parses it.
bool LoadGltf(const std::string& path, GltfModel& out,
              std::string* error = nullptr);

// Reads `path` through the vfs and parses it.
bool LoadGlb(const std::string& path, GltfModel& out,
             std::string* error = nullptr);
}

#endif
