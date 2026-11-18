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
constexpr size_t kMagicSize = 8;
constexpr size_t kHeader = kMagicSize + 8 + 8;  // magic, table size, table check

// FNV-1a 64-bit: the standard offset basis and prime. `seed` lets a caller salt the hash (the pack key).
constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

uint64_t fnv(const char* bytes, size_t count, uint64_t seed = kFnvOffsetBasis) {
    uint64_t hash = seed;
    for (size_t i = 0; i < count; ++i) {
        hash ^= static_cast<unsigned char>(bytes[i]);
        hash *= kFnvPrime;
    }
    return hash;
}

uint64_t mix(uint64_t z) {  // splitmix64 finaliser
    z += 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// Spreads consecutive keystream block numbers apart before mixing (a 64-bit odd constant).
constexpr uint64_t kBlockStride = 0x2545f4914f6cdd1dull;

// XORs data with a keystream that depends on the key and a per-stream salt (the file's path).
void crypt(std::string& data, uint64_t key, uint64_t salt) {
    const uint64_t seed = mix(key ^ salt);
    size_t pos = 0;
    for (uint64_t block = 0; pos < data.size(); ++block) {
        const uint64_t keystream = mix(seed + block * kBlockStride);  // 8 bytes of keystream per block
        for (int byte = 0; byte < 8 && pos < data.size(); ++byte, ++pos)
            data[pos] = static_cast<char>(data[pos] ^ static_cast<char>(keystream >> (8 * byte)));
    }
}

// Little-endian 64-bit integers.
void put64(std::string& out, uint64_t value) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>(value >> (8 * i)));
}
bool get64(const std::string& in, size_t& pos, uint64_t& value) {
    if (pos + 8 > in.size()) return false;
    value = 0;
    for (int i = 0; i < 8; ++i) value |= static_cast<uint64_t>(static_cast<unsigned char>(in[pos + i])) << (8 * i);
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
    static std::vector<Mount> all;
    return all;
}

std::string normalise(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string out;  // collapse "//", drop "./"
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '/' && !out.empty() && out.back() == '/') continue;
        if (path[i] == '.' && (i == 0 || path[i - 1] == '/') && i + 1 < path.size() && path[i + 1] == '/') {
            ++i;
            continue;
        }
        out.push_back(path[i]);
    }
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}

// The mount and relative path a path falls under, or null.
const Mount* resolve(const std::string& path, std::string& rel) {
    const std::string normalised = normalise(path);
    const Mount* best = nullptr;  // the deepest mount wins
    for (const Mount& mount : mounts()) {
        if (normalised.size() > mount.root.size() && normalised.compare(0, mount.root.size(), mount.root) == 0 &&
            normalised[mount.root.size()] == '/' && (!best || mount.root.size() > best->root.size())) {
            best = &mount;
            rel = normalised.substr(mount.root.size() + 1);
        }
    }
    return best;
}

bool readDisk(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream contents;
    contents << file.rdbuf();
    out = contents.str();
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
        std::string encrypted = content;
        crypt(encrypted, key, fnv(path.data(), path.size()));
        put64(table, path.size());
        table += path;
        put64(table, blob.size());
        put64(table, content.size());
        put64(table, fnv(content.data(), content.size(), key));
        blob += encrypted;
    }
    const uint64_t tableCheck = fnv(table.data(), table.size(), key);
    const uint64_t tableSize = table.size();
    crypt(table, key, 0);
    std::string out(kMagic, kMagicSize);
    put64(out, tableSize);
    put64(out, tableCheck);
    out += table;
    out += blob;
    return out;
}

bool mountMemory(std::string bytes, const std::string& root, uint64_t key, std::string* error) {
    if (bytes.size() < kHeader || std::memcmp(bytes.data(), kMagic, kMagicSize) != 0) return fail(error, "not an asset pack");
    size_t pos = kMagicSize;
    uint64_t tableSize = 0, tableCheck = 0;
    get64(bytes, pos, tableSize);
    get64(bytes, pos, tableCheck);
    if (tableSize > bytes.size() - kHeader) return fail(error, "asset pack is truncated");
    std::string table = bytes.substr(kHeader, static_cast<size_t>(tableSize));
    crypt(table, key, 0);
    if (fnv(table.data(), table.size(), key) != tableCheck) return fail(error, "asset pack is damaged or the key is wrong");

    Mount mount;
    mount.root = normalise(root);
    mount.key = key;
    const uint64_t dataStart = kHeader + tableSize;
    size_t tablePos = 0;
    uint64_t count = 0;
    if (!get64(table, tablePos, count)) return fail(error, "asset pack is damaged");
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t pathLength = 0;
        Entry entry;
        if (!get64(table, tablePos, pathLength) || pathLength > table.size() - tablePos) return fail(error, "asset pack is damaged");
        const std::string path = table.substr(tablePos, static_cast<size_t>(pathLength));
        tablePos += static_cast<size_t>(pathLength);
        if (!get64(table, tablePos, entry.offset) || !get64(table, tablePos, entry.size) || !get64(table, tablePos, entry.check))
            return fail(error, "asset pack is damaged");
        if (entry.offset > bytes.size() - dataStart || entry.size > bytes.size() - dataStart - entry.offset)
            return fail(error, "asset pack is truncated");
        entry.offset += dataStart;
        mount.files[path] = entry;
    }
    mount.bytes = std::move(bytes);
    auto& all = mounts();
    // Mounting the same root again replaces the earlier pack.
    all.erase(std::remove_if(all.begin(), all.end(), [&](const Mount& other) { return other.root == mount.root; }), all.end());
    all.push_back(std::move(mount));
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
    const Mount* mount = resolve(path, rel);
    if (!mount) return readDisk(path, out);
    auto found = mount->files.find(rel);
    if (found == mount->files.end()) return false;
    const Entry& entry = found->second;
    std::string data = mount->bytes.substr(static_cast<size_t>(entry.offset), static_cast<size_t>(entry.size));
    crypt(data, mount->key, fnv(rel.data(), rel.size()));
    if (fnv(data.data(), data.size(), mount->key) != entry.check) return false;
    out = std::move(data);
    return true;
}

bool exists(const std::string& path) {
    std::string rel;
    if (const Mount* mount = resolve(path, rel)) return mount->files.count(rel) > 0;
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::vector<std::string> list(const std::string& dir) {
    std::set<std::string> names;
    std::string rel;
    const std::string normalisedDir = normalise(dir);
    const Mount* mount = resolve(normalisedDir + "/x", rel);  // resolves the directory by probing a child
    if (mount) {
        const std::string prefix = rel.substr(0, rel.size() - 1);  // "dir/" or ""
        for (const auto& [path, entry] : mount->files) {
            (void)entry;
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
