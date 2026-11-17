#include "engine/core/fileio.h"

#include <cstdio>
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
        std::remove(path.c_str());
        return true;
    }
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) return false;
    }
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

}  // namespace mythos
