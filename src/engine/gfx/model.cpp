#include "engine/gfx/model.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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

// One corner of a face: position, texture coordinate and normal, already looked up.
struct Corner {
    Pos position;
    Uv uv;
    Pos normal;
};

// Resolves a 1-based OBJ index (negative = relative to the end) into a 0-based index.
bool resolveIndex(long idx, size_t count, size_t& out) {
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
    bool parseLine(std::istringstream& line, const std::string& tag) {
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
            current_ = batchFor(name);
            haveMaterial_ = true;
        } else if (tag == "f") {
            return parseFace(line);
        }
        // Other tags (o, g, s, mtllib, ...) are ignored.
        return true;
    }

private:
    // The batch for a material name, created on first use. "glow_x" means texture x, emissive.
    size_t batchFor(const std::string& material) {
        auto it = batchIndex_.find(material);
        if (it != batchIndex_.end()) return it->second;
        Batch batch;
        const std::string prefix = "glow_";
        if (material.compare(0, prefix.size(), prefix) == 0) {
            batch.texture = material.substr(prefix.size());
            batch.emissive = true;
        } else {
            batch.texture = material;
        }
        out_.push_back(batch);
        batchIndex_[material] = out_.size() - 1;
        return out_.size() - 1;
    }

    // Parses a face corner token: "v", "v/t", "v//n" or "v/t/n". False on a bad position index.
    bool parseCorner(const std::string& token, Corner& corner) const {
        const size_t firstSlash = token.find('/');
        const long positionIndex = std::strtol(token.c_str(), nullptr, 10);
        long uvIndex = 0, normalIndex = 0;
        size_t positionSlot = 0;
        if (!resolveIndex(positionIndex, positions_.size(), positionSlot)) return false;
        corner.position = positions_[positionSlot];
        corner.uv = Uv{0.0f, 0.0f};
        corner.normal = Pos{0.0f, 1.0f, 0.0f};
        if (firstSlash != std::string::npos) {
            const size_t secondSlash = token.find('/', firstSlash + 1);
            if (secondSlash == std::string::npos) {
                uvIndex = std::strtol(token.c_str() + firstSlash + 1, nullptr, 10);
            } else {
                if (secondSlash > firstSlash + 1) uvIndex = std::strtol(token.c_str() + firstSlash + 1, nullptr, 10);
                normalIndex = std::strtol(token.c_str() + secondSlash + 1, nullptr, 10);
            }
            size_t uvSlot = 0;
            if (uvIndex != 0 && resolveIndex(uvIndex, uvs_.size(), uvSlot)) corner.uv = uvs_[uvSlot];
            size_t normalSlot = 0;
            if (normalIndex != 0 && resolveIndex(normalIndex, normals_.size(), normalSlot)) corner.normal = normals_[normalSlot];
        }
        return true;
    }

    bool parseFace(std::istringstream& line) {
        if (!haveMaterial_) current_ = batchFor("missing");
        std::vector<Corner> corners;
        std::string token;
        while (line >> token) {
            Corner corner{};
            if (!parseCorner(token, corner)) return false;
            corners.push_back(corner);
        }
        if (corners.size() < 3) return false;
        // Fan triangulation (quads become two triangles).
        Batch& batch = out_[current_];
        for (size_t i = 1; i + 1 < corners.size(); ++i) {
            const Corner* triangle[3] = {&corners[0], &corners[i], &corners[i + 1]};
            for (const Corner* corner : triangle)
                batch.verts.push_back({corner->position.x, corner->position.y, corner->position.z, corner->normal.x,
                                       corner->normal.y, corner->normal.z, corner->uv.u, corner->uv.v});
        }
        return true;
    }

    std::vector<Batch>& out_;
    std::vector<Pos> positions_;
    std::vector<Uv> uvs_;
    std::vector<Pos> normals_;
    std::map<std::string, size_t> batchIndex_;
    size_t current_ = 0;
    bool haveMaterial_ = false;
};

}  // namespace

bool parseObj(const std::string& text, std::vector<Batch>& out, std::string* error) {
    ObjParser parser(out);
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream lineStream(line);
        std::string tag;
        if (!(lineStream >> tag) || tag[0] == '#') continue;
        if (!parser.parseLine(lineStream, tag)) {
            if (error) *error = "bad OBJ line " + std::to_string(lineNo) + ": " + line;
            return false;
        }
    }
    return true;
}

bool ModelLibrary::loadDir(const std::string& dir, std::string* error) {
    const std::vector<std::string> files = vfs::list(dir);
    if (files.empty()) {
        if (error) *error = "missing model folder " + dir;
        return false;
    }
    for (const std::string& file : files) {
        const std::filesystem::path name(file);
        if (name.extension() != ".obj") continue;
        std::string text;
        if (!vfs::readFile(dir + "/" + file, text)) {
            if (error) *error = dir + "/" + file + ": cannot read";
            return false;
        }
        Model m;
        m.name = name.stem().string();
        std::string err;
        if (!parseObj(text, m.batches, &err)) {
            if (error) *error = dir + "/" + file + ": " + err;
            return false;
        }
        models_[m.name] = std::move(m);
    }
    return !models_.empty();
}

const Model* ModelLibrary::find(const std::string& name) const {
    auto it = models_.find(name);
    return it != models_.end() ? &it->second : nullptr;
}

}  // namespace mythos
