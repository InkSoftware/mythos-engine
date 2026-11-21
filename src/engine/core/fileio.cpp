#include "engine/core/fileio.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace mythos {
std::string ReadTextFile(const std::string& path) {
  const std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

// The temporary file sits beside the target so the rename stays on one file
// system.
bool WriteFileAtomic(const std::string& path, const std::string& text,
                     const bool remove_if_empty) {
  if (path.empty()) return false;
  if (text.empty() && remove_if_empty) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return true;
  }
  const std::string temp_path = path + ".tmp";
  {
    std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
    out << text;
    if (!out) return false;
  }
  std::error_code ec;
  std::filesystem::rename(temp_path, path, ec);

  if (ec) std::filesystem::remove(temp_path, ec);
  return !ec;
}
}
