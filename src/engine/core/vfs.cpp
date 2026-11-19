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
constexpr size_t kHeader =
    kMagicSize + 8 + 8;  // magic, table size, table check

// FNV-1a 64-bit: the standard offset basis and prime. `seed` lets a caller salt
// the hash (the pack key).
constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

uint64_t Fnv(const char* bytes, size_t count, uint64_t seed = kFnvOffsetBasis) {
  uint64_t hash = seed;
  for (size_t i = 0; i < count; ++i) {
    hash ^= static_cast<unsigned char>(bytes[i]);
    hash *= kFnvPrime;
  }
  return hash;
}

uint64_t Mix(uint64_t z) {  // splitmix64 finaliser
  z += 0x9e3779b97f4a7c15ull;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

// Spreads consecutive keystream block numbers apart before mixing (a 64-bit odd
// constant).
constexpr uint64_t kBlockStride = 0x2545f4914f6cdd1dull;

// XORs data with a keystream that depends on the key and a per-stream salt (the
// file's path).
void Crypt(std::string& data, uint64_t key, uint64_t salt) {
  const uint64_t seed = Mix(key ^ salt);
  size_t pos = 0;
  for (uint64_t block = 0; pos < data.size(); ++block) {
    const uint64_t keystream =
        Mix(seed + block * kBlockStride);  // 8 bytes of keystream per block
    for (int byte = 0; byte < 8 && pos < data.size(); ++byte, ++pos) {
      data[pos] = static_cast<char>(data[pos] ^
                                    static_cast<char>(keystream >> (8 * byte)));
    }
  }
}

// Little-endian 64-bit integers.
void Put64(std::string& out, uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<char>(value >> (8 * i)));
  }
}
bool Get64(const std::string& in, size_t& pos, uint64_t& value) {
  if (pos + 8 > in.size()) return false;
  value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<uint64_t>(static_cast<unsigned char>(in[pos + i]))
             << (8 * i);
  }
  pos += 8;
  return true;
}

struct Entry {
  uint64_t offset = 0, size = 0, check = 0;
};

struct MountedPack {
  std::string root;  // normalised, no trailing '/'
  std::string bytes;
  uint64_t key = 0;
  std::map<std::string, Entry> files;  // by relative path
};

std::vector<MountedPack>& Mounts() {
  static std::vector<MountedPack> all;
  return all;
}

std::string Normalise(std::string path) {
  std::ranges::replace(path, '\\', '/');
  std::string out;  // collapse "//", drop "./"
  for (size_t i = 0; i < path.size(); ++i) {
    if (path[i] == '/' && !out.empty() && out.back() == '/') continue;
    if (path[i] == '.' && (i == 0 || path[i - 1] == '/') &&
        i + 1 < path.size() && path[i + 1] == '/') {
      ++i;
      continue;
    }
    out.push_back(path[i]);
  }
  while (out.size() > 1 && out.back() == '/') out.pop_back();
  return out;
}

// The mount and relative path a path falls under, or null.
const MountedPack* Resolve(const std::string& path, std::string& rel) {
  const std::string normalised = Normalise(path);
  const MountedPack* best = nullptr;  // the deepest mount wins
  for (const MountedPack& mount : Mounts()) {
    if (normalised.size() > mount.root.size() &&
        normalised.starts_with(mount.root) &&
        normalised[mount.root.size()] == '/' &&
        (!best || mount.root.size() > best->root.size())) {
      best = &mount;
      rel = normalised.substr(mount.root.size() + 1);
    }
  }
  return best;
}

bool ReadDisk(const std::string& path, std::string& out) {
  const std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  std::ostringstream contents;
  contents << file.rdbuf();
  out = contents.str();
  return true;
}

bool Fail(std::string* error, const std::string& why) {
  if (error) *error = why;
  return false;
}

}  // namespace

uint64_t KeyFromString(const std::string& text) {
  return Mix(Fnv(text.data(), text.size()));
}

std::string BuildPack(
    const std::vector<std::pair<std::string, std::string>>& files,
    uint64_t key) {
  std::string table;
  std::string blob;
  Put64(table, files.size());
  for (const auto& [raw_path, content] : files) {
    const std::string path = Normalise(raw_path);
    std::string encrypted = content;
    Crypt(encrypted, key, Fnv(path.data(), path.size()));
    Put64(table, path.size());
    table += path;
    Put64(table, blob.size());
    Put64(table, content.size());
    Put64(table, Fnv(content.data(), content.size(), key));
    blob += encrypted;
  }
  const uint64_t table_check = Fnv(table.data(), table.size(), key);
  const uint64_t table_size = table.size();
  Crypt(table, key, 0);
  std::string out(kMagic, kMagicSize);
  Put64(out, table_size);
  Put64(out, table_check);
  out += table;
  out += blob;
  return out;
}

bool MountMemory(std::string bytes, const std::string& root, uint64_t key,
                 std::string* error) {
  if (bytes.size() < kHeader ||
      std::memcmp(bytes.data(), kMagic, kMagicSize) != 0) {
    return Fail(error, "not an asset pack");
  }
  size_t pos = kMagicSize;
  uint64_t table_size = 0, table_check = 0;
  Get64(bytes, pos, table_size);
  Get64(bytes, pos, table_check);
  if (table_size > bytes.size() - kHeader) {
    return Fail(error, "asset pack is truncated");
  }
  std::string table = bytes.substr(kHeader, static_cast<size_t>(table_size));
  Crypt(table, key, 0);
  if (Fnv(table.data(), table.size(), key) != table_check) {
    return Fail(error, "asset pack is damaged or the key is wrong");
  }

  MountedPack mount;
  mount.root = Normalise(root);
  mount.key = key;
  const uint64_t data_start = kHeader + table_size;
  size_t table_pos = 0;
  uint64_t count = 0;
  if (!Get64(table, table_pos, count)) {
    return Fail(error, "asset pack is damaged");
  }
  for (uint64_t i = 0; i < count; ++i) {
    uint64_t path_length = 0;
    Entry entry;
    if (!Get64(table, table_pos, path_length) ||
        path_length > table.size() - table_pos) {
      return Fail(error, "asset pack is damaged");
    }
    const std::string path =
        table.substr(table_pos, static_cast<size_t>(path_length));
    table_pos += static_cast<size_t>(path_length);
    if (!Get64(table, table_pos, entry.offset) ||
        !Get64(table, table_pos, entry.size) ||
        !Get64(table, table_pos, entry.check)) {
      return Fail(error, "asset pack is damaged");
    }
    if (entry.offset > bytes.size() - data_start ||
        entry.size > bytes.size() - data_start - entry.offset) {
      return Fail(error, "asset pack is truncated");
    }
    entry.offset += data_start;
    mount.files[path] = entry;
  }
  mount.bytes = std::move(bytes);
  auto& all = Mounts();
  // Mounting the same root again replaces the earlier pack.
  std::erase_if(
      all, [&](const MountedPack& other) { return other.root == mount.root; });
  all.push_back(std::move(mount));
  return true;
}

bool Mount(const std::string& pack_file, const std::string& root, uint64_t key,
           std::string* error) {
  std::string bytes;
  if (!ReadDisk(pack_file, bytes)) {
    return Fail(error, "cannot read " + pack_file);
  }
  return MountMemory(std::move(bytes), root, key, error);
}

void UnmountAll() { Mounts().clear(); }

bool ReadFile(const std::string& path, std::string& out) {
  std::string rel;
  const MountedPack* mount = Resolve(path, rel);
  if (!mount) return ReadDisk(path, out);
  const auto found = mount->files.find(rel);
  if (found == mount->files.end()) return false;
  const Entry& entry = found->second;
  std::string data = mount->bytes.substr(static_cast<size_t>(entry.offset),
                                         static_cast<size_t>(entry.size));
  Crypt(data, mount->key, Fnv(rel.data(), rel.size()));
  if (Fnv(data.data(), data.size(), mount->key) != entry.check) return false;
  out = std::move(data);
  return true;
}

bool Exists(const std::string& path) {
  std::string rel;
  if (const MountedPack* mount = Resolve(path, rel)) {
    return mount->files.contains(rel);
  }
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

std::vector<std::string> List(const std::string& dir) {
  std::set<std::string> names;
  std::string rel;
  const std::string normalised_dir = Normalise(dir);
  const MountedPack* mount = Resolve(
      normalised_dir + "/x", rel);  // resolves the directory by probing a child
  if (mount) {
    const std::string prefix = rel.substr(0, rel.size() - 1);  // "dir/" or ""
    for (const auto& [path, entry] : mount->files) {
      (void)entry;
      if (path.size() <= prefix.size() || !path.starts_with(prefix)) continue;
      const std::string name = path.substr(prefix.size());
      if (name.find('/') == std::string::npos) names.insert(name);
    }
  } else {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
      if (entry.is_regular_file(ec)) {
        names.insert(entry.path().filename().string());
      }
    }
  }
  return {names.begin(), names.end()};
}

}  // namespace mythos::vfs
