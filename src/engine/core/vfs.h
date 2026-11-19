#ifndef MYTHOS_ENGINE_CORE_VFS_H_
#define MYTHOS_ENGINE_CORE_VFS_H_
// A read-only virtual file system for packed game assets.
//
// A pack is one file holding a game's assets (made by tools/pack,
// `mythos_pack`). Mount it under a root path and every read below that root is
// served from the pack instead of the disk, so the loose files do not need to
// ship and cannot be edited. Paths outside any mount fall through to the disk,
// so user files (settings, saves) keep working.
//
// This is obfuscation, not DRM: the key is in the executable, so a determined
// player can extract the data. It stops casual editing and makes the assets one
// opaque file.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mythos::vfs {

// A 64-bit key from a string (FNV-1a), so the key can be a build-time text
// constant.
uint64_t KeyFromString(const std::string& text);

// Builds a pack from (relative path, bytes) pairs. Paths use '/' and must be
// unique.
std::string BuildPack(
    const std::vector<std::pair<std::string, std::string>>& files,
    uint64_t key);

// Mounts the pack file at root (any directory string; reads of root + "/x" are
// served from the pack's "x"). Returns false, with a reason, if the file is
// missing, damaged or the key is wrong. Mounting again with the same root
// replaces the earlier mount.
bool Mount(const std::string& pack_file, const std::string& root, uint64_t key,
           std::string* error = nullptr);
// The same, from the pack's bytes (for tests).
bool MountMemory(std::string bytes, const std::string& root, uint64_t key,
                 std::string* error = nullptr);
void UnmountAll();

// A file's bytes: from a mounted pack when path is under its root (a file
// missing there is missing, the disk is not tried), otherwise from the disk.
// False if it cannot be read or fails its check.
bool ReadFile(const std::string& path, std::string& out);
bool Exists(const std::string& path);
// The names of the regular files directly inside dir (not the paths), sorted.
// Same lookup rules.
std::vector<std::string> List(const std::string& dir);

}  // namespace mythos::vfs

#endif  // MYTHOS_ENGINE_CORE_VFS_H_
