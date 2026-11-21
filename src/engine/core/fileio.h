// Plain file helpers on the real file system (see vfs.h for packed assets).
#ifndef MYTHOS_ENGINE_CORE_FILEIO_H_
#define MYTHOS_ENGINE_CORE_FILEIO_H_

#include <string>

namespace mythos {
// Returns the whole file as bytes, or an empty string if it cannot be opened.
std::string ReadTextFile(const std::string& path);

// Writes `text` to `path` through a temporary file and a rename, so a crash
// never leaves a half-written file. With `remove_if_empty`, empty text deletes
// the file instead. Returns false on failure.
bool WriteFileAtomic(const std::string& path, const std::string& text,
                     bool remove_if_empty = false);
}

#endif
