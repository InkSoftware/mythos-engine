#include "engine/gfx/model.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <sstream>

#include "engine/core/vfs.h"

namespace mythos {
namespace {

struct Pos {
  float x, y, z;
};
struct Uv {
  float u, v;
};

// One corner of a face: position, texture coordinate and normal, already looked
// up.
struct Corner {
  Pos position;
  Uv uv;
  Pos normal;
};

// Resolves a 1-based OBJ index (negative = relative to the end) into a 0-based
// index.
bool ResolveIndex(long idx, size_t count, size_t& out) {
  if (idx < 0) idx = static_cast<long>(count) + idx + 1;
  if (idx < 1 || static_cast<size_t>(idx) > count) return false;
  out = static_cast<size_t>(idx - 1);
  return true;
}

// Reads an OBJ file line by line into batches (one per material).
class ObjParser {
 public:
  explicit ObjParser(std::vector<Batch>& out) : out_(out) {}

  // Handles one line. False if it is malformed.
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
    // Other tags (o, g, s, mtllib, ...) are ignored.
    return true;
  }

 private:
  // The batch for a material name, created on first use. "glow_x" means texture
  // x, emissive.
  size_t BatchFor(const std::string& material) {
    const auto it = batch_index_.find(material);
    if (it != batch_index_.end()) return it->second;
    Batch batch;
    const std::string prefix = "glow_";
    if (material.starts_with(prefix)) {
      batch.texture = material.substr(prefix.size());
      batch.emissive = true;
    } else {
      batch.texture = material;
    }
    out_.push_back(batch);
    batch_index_[material] = out_.size() - 1;
    return out_.size() - 1;
  }

  // Parses a face corner token: "v", "v/t", "v//n" or "v/t/n". False on a bad
  // position index.
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
    // Fan triangulation (quads become two triangles).
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

}  // namespace

bool ParseObj(const std::string& text, std::vector<Batch>& out,
              std::string* error) {
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

bool ModelLibrary::LoadDir(const std::string& dir, std::string* error) {
  const std::vector<std::string> files = vfs::List(dir);
  if (files.empty()) {
    if (error) *error = "missing model folder " + dir;
    return false;
  }
  for (const std::string& file : files) {
    const std::filesystem::path name(file);
    if (name.extension() != ".obj") continue;
    std::string text;
    if (!vfs::ReadFile(dir + "/" + file, text)) {
      if (error) *error = dir + "/" + file + ": cannot read";
      return false;
    }
    Model m;
    m.name = name.stem().string();
    std::string err;
    if (!ParseObj(text, m.batches, &err)) {
      if (error) *error = dir + "/" + file + ": " + err;
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

}  // namespace mythos
