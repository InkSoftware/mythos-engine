#ifndef MYTHOS_ENGINE_CORE_FILEIO_H_
#define MYTHOS_ENGINE_CORE_FILEIO_H_
// Whole-file text I/O.

#include <string>

namespace mythos {

// The file's bytes, or an empty string if it cannot be read.
std::string ReadTextFile(const std::string& path);

// Writes text to path via path + ".tmp" and a rename, so a crash never leaves
// half a file. An empty text removes the file instead (when removeIfEmpty).
// Returns false on failure.
bool WriteFileAtomic(const std::string& path, const std::string& text,
                     bool remove_if_empty = false);

}  // namespace mythos

#endif  // MYTHOS_ENGINE_CORE_FILEIO_H_
