#include "engine/gfx/model.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <sstream>
#include <utility>

#include "engine/core/vfs.h"

namespace mythos {
namespace {
struct Pos {
  float x, y, z;
};

struct Uv {
  float u, v;
};

// One face corner after resolving its indices.
struct Corner {
  Pos position;
  Uv uv;
  Pos normal;
};

// Turns a 1-based OBJ index (negative counts back from the end) into a 0-based
// slot of a list of `count` items. False if it is out of range.
bool ResolveIndex(int64_t idx, const size_t count, size_t& out) {
  if (idx < 0) idx = static_cast<int64_t>(count) + idx + 1;
  if (idx < 1 || std::cmp_greater(idx, count)) return false;
  out = static_cast<size_t>(idx - 1);
  return true;
}

// Reads OBJ lines one at a time into batches, one per material. Vertex data is
// global to the file; faces refer to it by index.
class ObjParser {
 public:
  explicit ObjParser(std::vector<Batch>& out) : out_(out) {}

  // Handles one line whose first token `tag` is already read. Unknown tags are
  // ignored; false means a malformed known line.
  bool ParseLine(std::istringstream& line, const std::string& tag) {
    if (tag == "v") {
      Pos p{};
      if (!(line >> p.x >> p.y >> p.z)) return false;
      positions_.push_back(p);
    } else if (tag == "vt") {
      Uv t{};
      if (!(line >> t.u >> t.v)) return false;
      uvs_.push_back(t);
    } else if (tag == "vn") {
      Pos n{};
      if (!(line >> n.x >> n.y >> n.z)) return false;
      normals_.push_back(n);
    } else if (tag == "usemtl") {
      std::string name;
      if (!(line >> name)) return false;
      current_ = BatchFor(name);
      have_material_ = true;
    } else if (tag == "f") {
      return ParseFace(line);
    }
    return true;
  }

 private:
  // Index of the batch for a material, created on first use. A "glow_" prefix
  // makes it emissive and names the texture after the rest.
  size_t BatchFor(const std::string& material) {
    const auto it = batch_index_.find(material);
    if (it != batch_index_.end()) return it->second;
    Batch batch;
    const std::string prefix = "glow_";
    if (material.starts_with(prefix)) {
      batch.texture = material.substr(prefix.size());
      batch.emissive = true;
    } else if (material.starts_with("cutout_") && material.size() > 7) {
      // "cutout_leaf": the texture leaf, drawn with its transparent texels
      // cut away (leaves, fences, grass cards): the texture needs an alpha
      // channel (a PNG).
      batch.texture = material.substr(7);
      batch.alpha_test = true;
    } else if (material.starts_with("blend") && material.size() > 6 &&
               std::isdigit(static_cast<unsigned char>(material[5]))) {
      // "blend35_dust": the texture dust, blended at 35 percent.
      char* end = nullptr;
      const long percent = std::strtol(material.c_str() + 5, &end, 10);
      if (end && *end == '_' && *(end + 1) != '\0') {
        batch.texture = end + 1;
        batch.opacity =
            static_cast<float>(std::clamp<long>(percent, 1, 100)) / 100.0f;
      } else {
        batch.texture = material;
      }
    } else {
      batch.texture = material;
    }
    out_.push_back(batch);
    batch_index_[material] = out_.size() - 1;
    return out_.size() - 1;
  }

  // Parses one face corner token ("p", "p/t", "p//n" or "p/t/n"). Only the
  // position index must be valid; a missing or out-of-range uv or normal falls
  // back to (0, 0) and +y.
  bool ParseCorner(const std::string& token, Corner& corner) const {
    const size_t first_slash = token.find('/');
    const int64_t position_index = std::strtoll(token.c_str(), nullptr, 10);
    int64_t uv_index = 0, normal_index = 0;
    size_t position_slot = 0;
    if (!ResolveIndex(position_index, positions_.size(), position_slot)) {
      return false;
    }
    corner.position = positions_[position_slot];
    corner.uv = Uv{.u = 0.0f, .v = 0.0f};
    corner.normal = Pos{.x = 0.0f, .y = 1.0f, .z = 0.0f};
    if (first_slash != std::string::npos) {
      const size_t second_slash = token.find('/', first_slash + 1);
      if (second_slash == std::string::npos) {
        uv_index = std::strtoll(token.c_str() + first_slash + 1, nullptr, 10);
      } else {
        if (second_slash > first_slash + 1) {
          uv_index = std::strtoll(token.c_str() + first_slash + 1, nullptr, 10);
        }
        normal_index =
            std::strtoll(token.c_str() + second_slash + 1, nullptr, 10);
      }
      size_t uv_slot = 0;
      if (uv_index != 0 && ResolveIndex(uv_index, uvs_.size(), uv_slot)) {
        corner.uv = uvs_[uv_slot];
      }
      size_t normal_slot = 0;
      if (normal_index != 0 &&
          ResolveIndex(normal_index, normals_.size(), normal_slot)) {
        corner.normal = normals_[normal_slot];
      }
    }
    return true;
  }

  // Triangulates a polygon as a fan around its first corner, which is right for
  // the convex quads the tools write.
  bool ParseFace(std::istringstream& line) {
    if (!have_material_) current_ = BatchFor("missing");
    std::vector<Corner> corners;
    std::string token;
    while (line >> token) {
      Corner corner{};
      if (!ParseCorner(token, corner)) return false;
      corners.push_back(corner);
    }
    if (corners.size() < 3) return false;
    Batch& batch = out_[current_];
    for (size_t i = 1; i + 1 < corners.size(); ++i) {
      const Corner* triangle[3] = {&corners[0], &corners[i], &corners[i + 1]};
      for (const Corner* corner : triangle) {
        batch.verts.push_back({.px = corner->position.x,
                               .py = corner->position.y,
                               .pz = corner->position.z,
                               .nx = corner->normal.x,
                               .ny = corner->normal.y,
                               .nz = corner->normal.z,
                               .u = corner->uv.u,
                               .v = corner->uv.v});
      }
    }
    return true;
  }

  std::vector<Batch>& out_;
  std::vector<Pos> positions_;
  std::vector<Uv> uvs_;
  std::vector<Pos> normals_;
  std::map<std::string, size_t> batch_index_;
  size_t current_ = 0;
  bool have_material_ = false;
};
}

// Lines are split on LF, with a trailing CR dropped; '#' lines are comments.
bool ParseObj(const std::string& text, std::vector<Batch>& out,
              std::string* const error) {
  ObjParser parser(out);
  std::istringstream in(text);
  std::string line;
  int line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream line_stream(line);
    std::string tag;
    if (!(line_stream >> tag) || tag[0] == '#') continue;
    if (!parser.ParseLine(line_stream, tag)) {
      if (error) {
        *error = "bad OBJ line " + std::to_string(line_no) + ": " + line;
      }
      return false;
    }
  }
  return true;
}

bool ModelLibrary::LoadDir(const std::string& dir, std::string* const error) {
  const std::vector<std::string> files = vfs::List(dir);
  if (files.empty()) {
    if (error) *error = "missing model folder " + dir;
    return false;
  }
  for (const std::string& file : files) {
    const std::filesystem::path name(file);
    if (name.extension() != ".obj") continue;
    std::string text;
    std::string path = dir;
    path += '/';
    path += file;
    if (!vfs::ReadFile(path, text)) {
      if (error) *error = path + ": cannot read";
      return false;
    }
    Model m;
    m.name = name.stem().string();
    std::string err;
    if (!ParseObj(text, m.batches, &err)) {
      if (error) {
        *error = path;
        *error += ": ";
        *error += err;
      }
      return false;
    }
    models_[m.name] = std::move(m);
  }
  return !models_.empty();
}

const Model* ModelLibrary::Find(const std::string& name) const {
  const auto it = models_.find(name);
  return it != models_.end() ? &it->second : nullptr;
}
}
