#include "engine/core/vfs.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace mythos::vfs {
namespace {

constexpr char kMagic[8] = {'D', 'R', 'O', 'M', 'P', 'A', 'K', '1'};
constexpr size_t kHeader = 8 + 8 + 8;  // magic, table size, table check

uint64_t fnv(const char* p, size_t n, uint64_t h = 1469598103934665603ull) {
    for (size_t i = 0; i < n; ++i) {
        h ^= static_cast<unsigned char>(p[i]);
        h *= 1099511628211ull;
    }
    return h;
}

uint64_t mix(uint64_t z) {  // splitmix64 finaliser
    z += 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// XORs data with a keystream that depends on the key and a per-stream salt (the file's path).
void crypt(std::string& data, uint64_t key, uint64_t salt) {
    const uint64_t seed = mix(key ^ salt);
    size_t i = 0;
    for (uint64_t block = 0; i < data.size(); ++block) {
        const uint64_t k = mix(seed + block * 0x2545f4914f6cdd1dull);
        for (int b = 0; b < 8 && i < data.size(); ++b, ++i) data[i] = static_cast<char>(data[i] ^ static_cast<char>(k >> (8 * b)));
    }
}

void put64(std::string& s, uint64_t v) {
    for (int i = 0; i < 8; ++i) s.push_back(static_cast<char>(v >> (8 * i)));
}
bool get64(const std::string& s, size_t& pos, uint64_t& v) {
    if (pos + 8 > s.size()) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(static_cast<unsigned char>(s[pos + i])) << (8 * i);
    pos += 8;
    return true;
}

struct Entry {
    uint64_t offset = 0, size = 0, check = 0;
};

struct Mount {
    std::string root;  // normalised, no trailing '/'
    std::string bytes;
    uint64_t key = 0;
    std::map<std::string, Entry> files;  // by relative path
};

std::vector<Mount>& mounts() {
    static std::vector<Mount> m;
    return m;
}

std::string normalise(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    std::string out;  // collapse "//", drop "./"
    for (size_t i = 0; i < p.size(); ++i) {
        if (p[i] == '/' && !out.empty() && out.back() == '/') continue;
        if (p[i] == '.' && (i == 0 || p[i - 1] == '/') && i + 1 < p.size() && p[i + 1] == '/') {
            ++i;
            continue;
        }
        out.push_back(p[i]);
    }
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}

// The mount and relative path a path falls under, or null.
const Mount* resolve(const std::string& path, std::string& rel) {
    const std::string p = normalise(path);
    const Mount* best = nullptr;
    for (const Mount& m : mounts()) {
        if (p.size() > m.root.size() && p.compare(0, m.root.size(), m.root) == 0 && p[m.root.size()] == '/' &&
            (!best || m.root.size() > best->root.size())) {
            best = &m;
            rel = p.substr(m.root.size() + 1);
        }
    }
    return best;
}

bool readDisk(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

}  // namespace

uint64_t keyFromString(const std::string& text) { return mix(fnv(text.data(), text.size())); }

std::string buildPack(const std::vector<std::pair<std::string, std::string>>& files, uint64_t key) {
    std::string table;
    std::string blob;
    put64(table, files.size());
    for (const auto& [rawPath, content] : files) {
        const std::string path = normalise(rawPath);
        std::string enc = content;
        crypt(enc, key, fnv(path.data(), path.size()));
        put64(table, path.size());
        table += path;
        put64(table, blob.size());
        put64(table, content.size());
        put64(table, fnv(content.data(), content.size(), key));
        blob += enc;
    }
    const uint64_t tableCheck = fnv(table.data(), table.size(), key);
    const uint64_t tableSize = table.size();
    crypt(table, key, 0);
    std::string out(kMagic, 8);
    put64(out, tableSize);
    put64(out, tableCheck);
    out += table;
    out += blob;
    return out;
}

bool mountMemory(std::string bytes, const std::string& root, uint64_t key, std::string* error) {
    if (bytes.size() < kHeader || std::memcmp(bytes.data(), kMagic, 8) != 0) return fail(error, "not an asset pack");
    size_t pos = 8;
    uint64_t tableSize = 0, tableCheck = 0;
    get64(bytes, pos, tableSize);
    get64(bytes, pos, tableCheck);
    if (tableSize > bytes.size() - kHeader) return fail(error, "asset pack is truncated");
    std::string table = bytes.substr(kHeader, static_cast<size_t>(tableSize));
    crypt(table, key, 0);
    if (fnv(table.data(), table.size(), key) != tableCheck) return fail(error, "asset pack is damaged or the key is wrong");

    Mount m;
    m.root = normalise(root);
    m.key = key;
    const uint64_t dataStart = kHeader + tableSize;
    size_t tp = 0;
    uint64_t count = 0;
    if (!get64(table, tp, count)) return fail(error, "asset pack is damaged");
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t len = 0;
        Entry e;
        if (!get64(table, tp, len) || len > table.size() - tp) return fail(error, "asset pack is damaged");
        const std::string path = table.substr(tp, static_cast<size_t>(len));
        tp += static_cast<size_t>(len);
        if (!get64(table, tp, e.offset) || !get64(table, tp, e.size) || !get64(table, tp, e.check)) return fail(error, "asset pack is damaged");
        if (e.offset > bytes.size() - dataStart || e.size > bytes.size() - dataStart - e.offset) return fail(error, "asset pack is truncated");
        e.offset += dataStart;
        m.files[path] = e;
    }
    m.bytes = std::move(bytes);
    auto& all = mounts();
    all.erase(std::remove_if(all.begin(), all.end(), [&](const Mount& o) { return o.root == m.root; }), all.end());
    all.push_back(std::move(m));
    return true;
}

bool mount(const std::string& packFile, const std::string& root, uint64_t key, std::string* error) {
    std::string bytes;
    if (!readDisk(packFile, bytes)) return fail(error, "cannot read " + packFile);
    return mountMemory(std::move(bytes), root, key, error);
}

void unmountAll() { mounts().clear(); }
bool anyMounted() { return !mounts().empty(); }

bool readFile(const std::string& path, std::string& out) {
    std::string rel;
    const Mount* m = resolve(path, rel);
    if (!m) return readDisk(path, out);
    auto it = m->files.find(rel);
    if (it == m->files.end()) return false;
    const Entry& e = it->second;
    std::string data = m->bytes.substr(static_cast<size_t>(e.offset), static_cast<size_t>(e.size));
    crypt(data, m->key, fnv(rel.data(), rel.size()));
    if (fnv(data.data(), data.size(), m->key) != e.check) return false;
    out = std::move(data);
    return true;
}

bool exists(const std::string& path) {
    std::string rel;
    if (const Mount* m = resolve(path, rel)) return m->files.count(rel) > 0;
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::vector<std::string> list(const std::string& dir) {
    std::set<std::string> names;
    std::string rel;
    const std::string d = normalise(dir);
    const Mount* m = resolve(d + "/x", rel);  // resolves the directory by probing a child
    if (m) {
        const std::string prefix = rel.substr(0, rel.size() - 1);  // "dir/" or ""
        for (const auto& [path, e] : m->files) {
            (void)e;
            if (path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0) continue;
            const std::string name = path.substr(prefix.size());
            if (name.find('/') == std::string::npos) names.insert(name);
        }
    } else {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
            if (entry.is_regular_file(ec)) names.insert(entry.path().filename().string());
    }
    return {names.begin(), names.end()};
}

}  // namespace mythos::vfs
