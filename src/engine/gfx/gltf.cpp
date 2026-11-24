#include "engine/gfx/gltf.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <utility>

#include "engine/core/json.h"
#include "engine/core/mat4.h"
#include "engine/core/vfs.h"

namespace mythos {
namespace {
struct Fail {
  std::string message;
};

[[noreturn]] void Throw(std::string message) { throw Fail{std::move(message)}; }

uint32_t ReadU32(const std::string& s, const size_t at) {
  uint32_t v = 0;
  std::memcpy(&v, s.data() + at, 4);
  return v;  // glTF is little-endian; so is every supported target.
}

class Doc {
 public:
  Doc(json::Value root, std::string bin)
      : root_(std::move(root)), bin_(std::move(bin)) {}

  const json::Value& Root() const { return root_; }

  const json::Value& Item(const char* list, const int index) const {
    const json::Value* l = json::Find(root_, list);
    if (l == nullptr || !l->is_array() || index < 0 ||
        static_cast<size_t>(index) >= l->size()) {
      Throw(std::string("missing ") + list + " entry " + std::to_string(index));
    }
    return (*l)[static_cast<size_t>(index)];
  }

  // The values of an accessor as floats, `components` per element. Integer
  // types are normalised when the accessor says so.
  std::vector<float> Read(const int accessor_index, int* components_out) const {
    const json::Value& a = Item("accessors", accessor_index);
    if (a.contains("sparse")) Throw("sparse accessors are not supported");
    const int type = a.value("componentType", 0);
    const bool normalized = a.value("normalized", false);
    const size_t count = a.value("count", size_t{0});
    const std::string kind = a.value("type", std::string());
    static const std::map<std::string, int> kComponents = {
        {"SCALAR", 1}, {"VEC2", 2}, {"VEC3", 3}, {"VEC4", 4}, {"MAT4", 16}};
    const auto comp = kComponents.find(kind);
    if (comp == kComponents.end()) Throw("unsupported accessor type " + kind);
    const int n = comp->second;
    if (components_out != nullptr) *components_out = n;
    size_t size = 0;
    switch (type) {
      case 5120:
      case 5121:
        size = 1;
        break;
      case 5122:
      case 5123:
        size = 2;
        break;
      case 5125:
      case 5126:
        size = 4;
        break;
      default:
        Throw("unsupported component type " + std::to_string(type));
    }
    if (!a.contains("bufferView")) Throw("accessor without a buffer view");
    const json::Value& view = Item("bufferViews", a["bufferView"].get<int>());
    if (view.value("buffer", 0) != 0) Throw("only one buffer is supported");
    const size_t base =
        view.value("byteOffset", size_t{0}) + a.value("byteOffset", size_t{0});
    const size_t stride = view.value("byteStride", size_t{0}) != 0
                              ? view["byteStride"].get<size_t>()
                              : size * static_cast<size_t>(n);
    std::vector<float> out;
    out.reserve(count * static_cast<size_t>(n));
    for (size_t i = 0; i < count; ++i) {
      for (int c = 0; c < n; ++c) {
        const size_t at = base + i * stride + static_cast<size_t>(c) * size;
        if (at + size > bin_.size()) Throw("accessor reads past the buffer");
        const char* p = bin_.data() + at;
        float v = 0;
        switch (type) {
          case 5120: {
            int8_t x;
            std::memcpy(&x, p, 1);
            v = normalized ? std::max(x / 127.0f, -1.0f)
                           : static_cast<float>(x);
            break;
          }
          case 5121: {
            uint8_t x;
            std::memcpy(&x, p, 1);
            v = normalized ? x / 255.0f : static_cast<float>(x);
            break;
          }
          case 5122: {
            int16_t x;
            std::memcpy(&x, p, 2);
            v = normalized ? std::max(x / 32767.0f, -1.0f)
                           : static_cast<float>(x);
            break;
          }
          case 5123: {
            uint16_t x;
            std::memcpy(&x, p, 2);
            v = normalized ? x / 65535.0f : static_cast<float>(x);
            break;
          }
          case 5125: {
            uint32_t x;
            std::memcpy(&x, p, 4);
            v = static_cast<float>(x);
            break;
          }
          default:
            std::memcpy(&v, p, 4);
            break;
        }
        out.push_back(v);
      }
    }
    return out;
  }

 private:
  json::Value root_;
  std::string bin_;
};

BoneTransform NodeTransform(const json::Value& node) {
  BoneTransform t;
  if (node.contains("matrix") && node["matrix"].is_array() &&
      node["matrix"].size() == 16) {
    std::array<double, 16> m{};
    for (size_t i = 0; i < 16; ++i) m[i] = node["matrix"][i].get<double>();
    t.t = {m[12], m[13], m[14]};
    const auto len = [&](int c) {
      return std::sqrt(m[c * 4] * m[c * 4] + m[c * 4 + 1] * m[c * 4 + 1] +
                       m[c * 4 + 2] * m[c * 4 + 2]);
    };
    t.s = {len(0), len(1), len(2)};
    // Rotation matrix to quaternion (columns divided by their scale).
    const double r00 = m[0] / t.s.x, r10 = m[1] / t.s.x, r20 = m[2] / t.s.x;
    const double r01 = m[4] / t.s.y, r11 = m[5] / t.s.y, r21 = m[6] / t.s.y;
    const double r02 = m[8] / t.s.z, r12 = m[9] / t.s.z, r22 = m[10] / t.s.z;
    const double tr = r00 + r11 + r22;
    if (tr > 0) {
      const double s = std::sqrt(tr + 1) * 2;
      t.r = {(r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s, s / 4};
    } else if (r00 > r11 && r00 > r22) {
      const double s = std::sqrt(1 + r00 - r11 - r22) * 2;
      t.r = {s / 4, (r01 + r10) / s, (r02 + r20) / s, (r21 - r12) / s};
    } else if (r11 > r22) {
      const double s = std::sqrt(1 + r11 - r00 - r22) * 2;
      t.r = {(r01 + r10) / s, s / 4, (r12 + r21) / s, (r02 - r20) / s};
    } else {
      const double s = std::sqrt(1 + r22 - r00 - r11) * 2;
      t.r = {(r02 + r20) / s, (r12 + r21) / s, s / 4, (r10 - r01) / s};
    }
    t.r = Normalize(t.r);
    return t;
  }
  if (node.contains("translation") && node["translation"].size() == 3) {
    t.t = {node["translation"][0].get<double>(),
           node["translation"][1].get<double>(),
           node["translation"][2].get<double>()};
  }
  if (node.contains("rotation") && node["rotation"].size() == 4) {
    t.r = Normalize(
        {node["rotation"][0].get<double>(), node["rotation"][1].get<double>(),
         node["rotation"][2].get<double>(), node["rotation"][3].get<double>()});
  }
  if (node.contains("scale") && node["scale"].size() == 3) {
    t.s = {node["scale"][0].get<double>(), node["scale"][1].get<double>(),
           node["scale"][2].get<double>()};
  }
  return t;
}

bool IsIdentity(const BoneTransform& t) {
  const auto near = [](double a, double b) { return std::fabs(a - b) < 1e-9; };
  return near(t.t.x, 0) && near(t.t.y, 0) && near(t.t.z, 0) && near(t.r.x, 0) &&
         near(t.r.y, 0) && near(t.r.z, 0) && near(std::fabs(t.r.w), 1) &&
         near(t.s.x, 1) && near(t.s.y, 1) && near(t.s.z, 1);
}

BoneTransform FromMatrix(const Mat4f& m) {
  json::Value node = json::Value::object();
  node["matrix"] = json::Value::array();
  for (const float v : m) node["matrix"].push_back(v);
  return NodeTransform(node);
}

Mat4f Multiply(const Mat4f& a, const Mat4f& b) {
  Mat4f out;
  Mat4Multiply(a.data(), b.data(), out.data());
  return out;
}

void Build(const Doc& doc, GltfModel& out) {
  const json::Value& root = doc.Root();
  const json::Value* nodes = json::Find(root, "nodes");
  if (nodes == nullptr || !nodes->is_array()) Throw("no nodes");
  const int node_count = static_cast<int>(nodes->size());

  // Parent of every node, from the children lists.
  std::vector<int> parent(static_cast<size_t>(node_count), -1);
  for (int i = 0; i < node_count; ++i) {
    const json::Value* kids =
        json::Find((*nodes)[static_cast<size_t>(i)], "children");
    if (kids == nullptr) continue;
    for (const auto& k : *kids) {
      const int c = k.get<int>();
      if (c >= 0 && c < node_count) parent[static_cast<size_t>(c)] = i;
    }
  }

  // The first node with both a mesh and a skin.
  int mesh_node = -1;
  for (int i = 0; i < node_count && mesh_node < 0; ++i) {
    const json::Value& n = (*nodes)[static_cast<size_t>(i)];
    if (n.contains("mesh") && n.contains("skin")) mesh_node = i;
  }
  if (mesh_node < 0) Throw("no skinned mesh");
  const json::Value& mesh_n = (*nodes)[static_cast<size_t>(mesh_node)];
  const json::Value& skin = doc.Item("skins", mesh_n["skin"].get<int>());
  std::vector<int> joint_nodes;
  for (const auto& j : skin["joints"]) joint_nodes.push_back(j.get<int>());
  if (joint_nodes.empty()) Throw("a skin without joints");

  // Bones in an order with parents first; joints keep the file's order unless
  // a child is listed before its parent. A synthetic root carries the
  // transform of the non-joint ancestors of the root joints.
  std::map<int, int> joint_index;  // node -> index in the skin's joint list
  for (size_t i = 0; i < joint_nodes.size(); ++i) {
    joint_index[joint_nodes[i]] = static_cast<int>(i);
  }
  const auto is_joint = [&](int n) { return joint_index.contains(n); };
  Mat4f ancestors = ToMatrix(BoneTransform{});
  bool have_ancestors = false;
  {
    // Ancestors of the first root joint (all roots of one skin share them in
    // practice).
    for (const int j : joint_nodes) {
      if (parent[static_cast<size_t>(j)] >= 0 &&
          is_joint(parent[static_cast<size_t>(j)])) {
        continue;
      }
      std::vector<int> chain;
      for (int p = parent[static_cast<size_t>(j)]; p >= 0;
           p = parent[static_cast<size_t>(p)]) {
        chain.push_back(p);
      }
      for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        ancestors = Multiply(
            ancestors,
            ToMatrix(NodeTransform((*nodes)[static_cast<size_t>(*it)])));
      }
      have_ancestors = !IsIdentity(FromMatrix(ancestors));
      break;
    }
  }

  std::vector<Bone> bones;
  std::map<int, int> bone_of_node;
  if (have_ancestors) {
    Bone root;
    root.name = "armature";
    root.bind = FromMatrix(ancestors);
    bones.push_back(root);
  }
  std::vector<bool> placed(joint_nodes.size(), false);
  size_t remaining = joint_nodes.size();
  while (remaining > 0) {
    const size_t before = remaining;
    for (size_t i = 0; i < joint_nodes.size(); ++i) {
      if (placed[i]) continue;
      const int node = joint_nodes[i];
      const int p = parent[static_cast<size_t>(node)];
      int parent_bone = have_ancestors ? 0 : -1;
      if (p >= 0 && is_joint(p)) {
        const auto it = bone_of_node.find(p);
        if (it == bone_of_node.end()) continue;  // parent not placed yet
        parent_bone = it->second;
      }
      Bone b;
      b.name = (*nodes)[static_cast<size_t>(node)].value("name", std::string());
      b.parent = parent_bone;
      b.bind = NodeTransform((*nodes)[static_cast<size_t>(node)]);
      bone_of_node[node] = static_cast<int>(bones.size());
      bones.push_back(std::move(b));
      placed[i] = true;
      --remaining;
    }
    if (remaining == before) Throw("the joints do not form a tree");
  }
  if (!out.skeleton.Set(bones)) Throw("too many joints or a bad hierarchy");
  // Skin joint list index -> bone index.
  std::vector<float> joint_to_bone(joint_nodes.size());
  for (size_t i = 0; i < joint_nodes.size(); ++i) {
    joint_to_bone[i] = static_cast<float>(bone_of_node[joint_nodes[i]]);
  }

  // The mesh.
  const json::Value& mesh = doc.Item("meshes", mesh_n["mesh"].get<int>());
  for (const auto& prim : mesh["primitives"]) {
    if (prim.value("mode", 4) != 4) Throw("only triangle primitives");
    const json::Value& attrs = prim["attributes"];
    if (!attrs.contains("POSITION") || !attrs.contains("JOINTS_0") ||
        !attrs.contains("WEIGHTS_0")) {
      Throw("a primitive without positions, joints and weights");
    }
    int c = 0;
    const std::vector<float> pos = doc.Read(attrs["POSITION"].get<int>(), &c);
    const size_t vcount = pos.size() / 3;
    std::vector<float> nrm, uv;
    if (attrs.contains("NORMAL"))
      nrm = doc.Read(attrs["NORMAL"].get<int>(), &c);
    if (attrs.contains("TEXCOORD_0")) {
      uv = doc.Read(attrs["TEXCOORD_0"].get<int>(), &c);
    }
    const std::vector<float> joints =
        doc.Read(attrs["JOINTS_0"].get<int>(), &c);
    const std::vector<float> weights =
        doc.Read(attrs["WEIGHTS_0"].get<int>(), &c);
    if (joints.size() != vcount * 4 || weights.size() != vcount * 4) {
      Throw("joint and weight counts do not match the positions");
    }
    std::vector<size_t> index;
    if (prim.contains("indices")) {
      for (const float f : doc.Read(prim["indices"].get<int>(), &c)) {
        index.push_back(static_cast<size_t>(f));
      }
    } else {
      for (size_t i = 0; i < vcount; ++i) index.push_back(i);
    }
    Batch batch;
    std::array<float, 4> color = {1, 1, 1, 1};
    batch.texture = "missing";
    if (prim.contains("material")) {
      const json::Value& mat =
          doc.Item("materials", prim["material"].get<int>());
      const std::string name = mat.value("name", std::string());
      if (!name.empty()) batch.texture = name;
      if (const json::Value* pbr = json::Find(mat, "pbrMetallicRoughness")) {
        if (const json::Value* f = json::Find(*pbr, "baseColorFactor");
            f != nullptr && f->size() == 4) {
          for (size_t i = 0; i < 4; ++i) color[i] = (*f)[i].get<float>();
        }
      }
    }
    for (size_t i = 0; i + 2 < index.size(); i += 3) {
      for (size_t k = 0; k < 3; ++k) {
        const size_t v = index[i + k];
        if (v >= vcount) Throw("an index past the vertices");
        Vertex vert{};
        vert.px = pos[v * 3];
        vert.py = pos[v * 3 + 1];
        vert.pz = pos[v * 3 + 2];
        if (!nrm.empty()) {
          vert.nx = nrm[v * 3];
          vert.ny = nrm[v * 3 + 1];
          vert.nz = nrm[v * 3 + 2];
        } else {
          vert.ny = 1;
        }
        if (!uv.empty()) {
          vert.u = uv[v * 2];
          vert.v = uv[v * 2 + 1];
        }
        batch.verts.push_back(vert);
        SkinWeights w;
        float sum = 0;
        for (size_t j = 0; j < 4; ++j) {
          const auto joint = static_cast<size_t>(joints[v * 4 + j]);
          w.weights[j] = weights[v * 4 + j];
          // A weightless slot may name any joint; keep it in range.
          w.joints[j] = w.weights[j] > 0 && joint < joint_to_bone.size()
                            ? joint_to_bone[joint]
                            : 0;
          sum += w.weights[j];
        }
        if (sum > 1e-6f) {
          for (float& x : w.weights) x /= sum;
        } else {
          w.weights[0] = 1;
        }
        batch.skin.push_back(w);
      }
    }
    if (batch.verts.empty()) continue;
    out.batches.push_back(std::move(batch));
    out.base_colors.push_back(color);
  }
  if (out.batches.empty()) Throw("the mesh has no triangles");

  // Animations.
  if (const json::Value* anims = json::Find(root, "animations")) {
    for (const auto& a : *anims) {
      Clip clip;
      clip.name = a.value("name", std::string());
      std::map<int, BoneTrack> tracks;
      for (const auto& ch : a["channels"]) {
        const json::Value& target = ch["target"];
        if (!target.contains("node")) continue;
        const auto bone = bone_of_node.find(target["node"].get<int>());
        if (bone == bone_of_node.end()) continue;  // not a joint
        const json::Value& sampler = a["samplers"][ch["sampler"].get<size_t>()];
        int cin = 0, cout = 0;
        const std::vector<float> times =
            doc.Read(sampler["input"].get<int>(), &cin);
        const std::vector<float> vals =
            doc.Read(sampler["output"].get<int>(), &cout);
        const std::string interp =
            sampler.value("interpolation", std::string("LINEAR"));
        const std::string path = target.value("path", std::string());
        const size_t stride_vals = interp == "CUBICSPLINE" ? 3 : 1;
        const size_t comps = static_cast<size_t>(cout);
        if (vals.size() < times.size() * comps * stride_vals) {
          Throw("an animation sampler with too few values");
        }
        BoneTrack& track = tracks[bone->second];
        track.bone = bone->second;
        for (size_t i = 0; i < times.size(); ++i) {
          // Cubic splines store in-tangent, value, out-tangent: take the value.
          const size_t at =
              (i * stride_vals + (stride_vals == 3 ? 1 : 0)) * comps;
          const double t = times[i];
          clip.duration = std::max(clip.duration, t);
          if (path == "translation") {
            track.translation.push_back(
                {t, {vals[at], vals[at + 1], vals[at + 2]}});
          } else if (path == "scale") {
            track.scale.push_back({t, {vals[at], vals[at + 1], vals[at + 2]}});
          } else if (path == "rotation") {
            track.rotation.push_back(
                {t, Normalize(
                        {vals[at], vals[at + 1], vals[at + 2], vals[at + 3]})});
          }
        }
      }
      for (auto& [bone, track] : tracks)
        clip.tracks.push_back(std::move(track));
      out.clips.push_back(std::move(clip));
    }
  }
}
}

const Clip* GltfModel::FindClip(const std::string& name) const {
  for (const Clip& c : clips) {
    if (c.name == name) return &c;
  }
  return nullptr;
}

bool ParseGlb(const std::string& bytes, GltfModel& out, std::string* error) {
  try {
    if (bytes.size() < 20 || bytes.compare(0, 4, "glTF") != 0) {
      Throw("not a binary glTF (.glb) file");
    }
    if (ReadU32(bytes, 4) != 2) Throw("only glTF version 2 is supported");
    std::string json_text, bin;
    size_t at = 12;
    while (at + 8 <= bytes.size()) {
      const size_t len = ReadU32(bytes, at);
      const uint32_t type = ReadU32(bytes, at + 4);
      if (at + 8 + len > bytes.size()) Throw("a chunk runs past the file");
      if (type == 0x4E4F534A) {  // JSON
        json_text = bytes.substr(at + 8, len);
      } else if (type == 0x004E4942 && bin.empty()) {  // BIN
        bin = bytes.substr(at + 8, len);
      }
      at += 8 + len;
      at = (at + 3) & ~size_t{3};
    }
    json::Value root;
    std::string parse_error;
    if (json_text.empty() || !json::Parse(json_text, root, &parse_error)) {
      Throw("bad JSON chunk: " + parse_error);
    }
    GltfModel model;
    Build(Doc(std::move(root), std::move(bin)), model);
    out = std::move(model);
    return true;
  } catch (const Fail& f) {
    if (error != nullptr) *error = f.message;
  } catch (const std::exception& e) {
    if (error != nullptr) *error = std::string("malformed glTF: ") + e.what();
  }
  return false;
}

bool LoadGlb(const std::string& path, GltfModel& out, std::string* error) {
  std::string bytes;
  if (!vfs::ReadFile(path, bytes)) {
    if (error != nullptr) *error = "cannot read " + path;
    return false;
  }
  return ParseGlb(bytes, out, error);
}
}
