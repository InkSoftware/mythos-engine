// Virtual file system: asset packs mounted over a directory prefix, with
// fallthrough to the disk. A pack is obfuscated (a keyed stream cipher plus
// checksums) to deter casual extraction, not secure encryption. The mount table
// is process-global and not thread-safe; mount before starting threads that
// read files.
#ifndef MYTHOS_ENGINE_CORE_VFS_H_
#define MYTHOS_ENGINE_CORE_VFS_H_

#include <string>
#include <utility>
#include <vector>

namespace mythos::vfs {
// Derives a pack key from a passphrase.
uint64_t KeyFromString(const std::string& text);

// Serializes (path, content) pairs into one pack image encrypted with `key`.
std::string BuildPack(
    const std::vector<std::pair<std::string, std::string>>& files,
    uint64_t key);

// Reads `pack_file` from disk and mounts it so that paths under `root` are
// served from the pack. Remounting the same root replaces it. Returns false
// with a message in `*error` if the file is unreadable, malformed or the key is
// wrong.
bool Mount(const std::string& pack_file, const std::string& root, uint64_t key,
           std::string* error = nullptr);

// As Mount, for a pack already in memory (taken by value).
bool MountMemory(std::string bytes, const std::string& root, uint64_t key,
                 std::string* error = nullptr);

// Drops every mounted pack.
void UnmountAll();

// Reads a file from the deepest pack whose root contains `path`, else from the
// disk. Returns false if it is missing or fails its checksum.
bool ReadFile(const std::string& path, std::string& out);

// Whether the file exists in a pack or on disk.
bool Exists(const std::string& path);

// Sorted names of the files (not directories) directly inside `dir`, from a
// pack or the disk.
std::vector<std::string> List(const std::string& dir);
}

#endif
