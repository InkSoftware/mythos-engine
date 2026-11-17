#include "engine/core/fileio.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace mythos {

std::string readTextFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool writeFileAtomic(const std::string& path, const std::string& text, bool removeIfEmpty) {
    if (path.empty()) return false;
    if (text.empty() && removeIfEmpty) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return true;
    }
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);  // replaces an existing file in one step (POSIX; MoveFileEx on Windows)
    if (ec) std::filesystem::remove(tmp, ec);
    return !ec;
}

}  // namespace mythos
