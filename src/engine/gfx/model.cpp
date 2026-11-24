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
Model BoundingBoxModel(const Model& model) {
  Model out;
  out.name = model.name;
  for (const Batch& b : model.batches) {
    if (b.opacity < 1.0f || !b.skin.empty() || b.verts.empty()) continue;
    float lo[3] = {b.verts[0].px, b.verts[0].py, b.verts[0].pz};
    float hi[3] = {lo[0], lo[1], lo[2]};
    for (const Vertex& v : b.verts) {
      const float p[3] = {v.px, v.py, v.pz};
      for (int i = 0; i < 3; ++i) {
        lo[i] = std::min(lo[i], p[i]);
        hi[i] = std::max(hi[i], p[i]);
      }
    }
    Batch box;
    box.texture = b.texture;
    box.emissive = b.emissive;
    box.alpha_test = false;
    const auto quad = [&](const float a[3], const float c[3], const float d[3],
                          const float e[3], const float nx, const float ny,
                          const float nz) {
      const float* corners[4] = {a, c, d, e};
      const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
      const int order[6] = {0, 1, 2, 0, 2, 3};
      for (const int k : order) {
        box.verts.push_back({corners[k][0], corners[k][1], corners[k][2], nx,
                             ny, nz, uv[k][0], uv[k][1]});
      }
    };
    const float x0 = lo[0], y0 = lo[1], z0 = lo[2];
    const float x1 = hi[0], y1 = hi[1], z1 = hi[2];
    const float c000[3] = {x0, y0, z0}, c100[3] = {x1, y0, z0},
                c110[3] = {x1, y1, z0}, c010[3] = {x0, y1, z0},
                c001[3] = {x0, y0, z1}, c101[3] = {x1, y0, z1},
                c111[3] = {x1, y1, z1}, c011[3] = {x0, y1, z1};
    quad(c001, c101, c111, c011, 0, 0, 1);   // front (+z)
    quad(c100, c000, c010, c110, 0, 0, -1);  // back
    quad(c101, c100, c110, c111, 1, 0, 0);   // +x
    quad(c000, c001, c011, c010, -1, 0, 0);  // -x
    quad(c011, c111, c110, c010, 0, 1, 0);   // top
    quad(c000, c100, c101, c001, 0, -1, 0);  // bottom
    out.batches.push_back(std::move(box));
  }
  return out;
}

ModelLibrary ModelLibrary::BoundingBoxes() const {
  ModelLibrary out;
  for (const auto& [name, model] : models_) out.Set(BoundingBoxModel(model));
  return out;
}

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

bool ModelLibrary::LoadFile(const std::string& path, std::string* const error) {
  std::string text;
  if (!vfs::ReadFile(path, text)) {
    if (error) *error = path + ": cannot read";
    return false;
  }
  Model m;
  m.name = std::filesystem::path(path).stem().string();
  std::string err;
  if (!ParseObj(text, m.batches, &err)) {
    if (error) *error = path + ": " + err;
    return false;
  }
  models_[m.name] = std::move(m);
  return true;
}

const Model* ModelLibrary::Find(const std::string& name) const {
  const auto it = models_.find(name);
  return it != models_.end() ? &it->second : nullptr;
}
}
