#include "engine/core/fileio.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace mythos {

std::string readTextFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::string();
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

bool writeFileAtomic(const std::string& path, const std::string& text, bool removeIfEmpty) {
    if (path.empty()) return false;
    if (text.empty() && removeIfEmpty) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return true;
    }
    const std::string tempPath = path + ".tmp";  // written whole, then renamed over the target
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tempPath, path, ec);  // replaces an existing file in one step (POSIX; MoveFileEx on Windows)
    if (ec) std::filesystem::remove(tempPath, ec);
    return !ec;
}

}  // namespace mythos
