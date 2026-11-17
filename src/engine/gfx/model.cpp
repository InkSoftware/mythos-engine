#include "engine/gfx/model.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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

// Resolves a 1-based OBJ index (negative = relative to the end) into a 0-based index.
bool resolveIndex(long idx, size_t count, size_t& out) {
    if (idx < 0) idx = static_cast<long>(count) + idx + 1;
    if (idx < 1 || static_cast<size_t>(idx) > count) return false;
    out = static_cast<size_t>(idx - 1);
    return true;
}

}  // namespace

bool parseObj(const std::string& text, std::vector<Batch>& out, std::string* error) {
    std::vector<Pos> positions;
    std::vector<Uv> uvs;
    std::vector<Pos> normals;
    std::map<std::string, size_t> batchIndex;
    size_t current = 0;
    bool haveMaterial = false;

    auto batchFor = [&](const std::string& material) -> size_t {
        auto it = batchIndex.find(material);
        if (it != batchIndex.end()) return it->second;
        Batch b;
        const std::string prefix = "glow_";
        if (material.compare(0, prefix.size(), prefix) == 0) {
            b.texture = material.substr(prefix.size());
            b.emissive = true;
        } else {
            b.texture = material;
        }
        out.push_back(b);
        batchIndex[material] = out.size() - 1;
        return out.size() - 1;
    };

    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string tag;
        if (!(ls >> tag) || tag[0] == '#') continue;

        if (tag == "v") {
            Pos p{};
            if (!(ls >> p.x >> p.y >> p.z)) goto bad;
            positions.push_back(p);
        } else if (tag == "vt") {
            Uv t{};
            if (!(ls >> t.u >> t.v)) goto bad;
            uvs.push_back(t);
        } else if (tag == "vn") {
            Pos n{};
            if (!(ls >> n.x >> n.y >> n.z)) goto bad;
            normals.push_back(n);
        } else if (tag == "usemtl") {
            std::string name;
            if (!(ls >> name)) goto bad;
            current = batchFor(name);
            haveMaterial = true;
        } else if (tag == "f") {
            if (!haveMaterial) current = batchFor("missing");
            // Collect the corners of the face.
            struct Corner {
                Pos p;
                Uv t;
                Pos n;
            };
            std::vector<Corner> corners;
            std::string tok;
            while (ls >> tok) {
                // tok is "v", "v/t", "v//n" or "v/t/n"
                long vi = 0, ti = 0, ni = 0;
                const size_t s1 = tok.find('/');
                vi = std::strtol(tok.c_str(), nullptr, 10);
                Corner c{};
                size_t pv = 0;
                if (!resolveIndex(vi, positions.size(), pv)) goto bad;
                c.p = positions[pv];
                c.t = Uv{0.0f, 0.0f};
                c.n = Pos{0.0f, 1.0f, 0.0f};
                if (s1 != std::string::npos) {
                    const size_t s2 = tok.find('/', s1 + 1);
                    if (s2 == std::string::npos) {
                        ti = std::strtol(tok.c_str() + s1 + 1, nullptr, 10);
                    } else {
                        if (s2 > s1 + 1) ti = std::strtol(tok.c_str() + s1 + 1, nullptr, 10);
                        ni = std::strtol(tok.c_str() + s2 + 1, nullptr, 10);
                    }
                    size_t pt = 0;
                    if (ti != 0 && resolveIndex(ti, uvs.size(), pt)) c.t = uvs[pt];
                    size_t pn = 0;
                    if (ni != 0 && resolveIndex(ni, normals.size(), pn)) c.n = normals[pn];
                }
                corners.push_back(c);
            }
            if (corners.size() < 3) goto bad;
            // Fan triangulation (quads become two triangles).
            Batch& b = out[current];
            for (size_t i = 1; i + 1 < corners.size(); ++i) {
                const Corner* tri[3] = {&corners[0], &corners[i], &corners[i + 1]};
                for (const Corner* c : tri)
                    b.verts.push_back({c->p.x, c->p.y, c->p.z, c->n.x, c->n.y, c->n.z, c->t.u, c->t.v});
            }
        }
        // Other tags (o, g, s, mtllib, ...) are ignored.
        continue;
    bad:
        if (error) *error = "bad OBJ line " + std::to_string(lineNo) + ": " + line;
        return false;
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
