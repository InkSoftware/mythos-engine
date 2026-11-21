

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/vfs.h"

// Packs a folder of assets into one encrypted .pak that the engine's VFS can
// mount. Usage: mythos_pack <out.pak> <assets dir> <key text>. Dot files are
// skipped and entries are sorted by path so the output is reproducible. Returns
// 2 on bad usage and 1 on an I/O error.
int main(const int argc, char* const* const argv) {
  namespace fs = std::filesystem;
  if (argc != 4) {
    std::fprintf(stderr,
                 "usage: mythos_pack <out.pak> <assets dir> <key text>\n");
    return 2;
  }
  const fs::path out = argv[1], dir = argv[2];
  if (!fs::is_directory(dir)) {
    std::fprintf(stderr, "mythos_pack: %s is not a folder\n",
                 dir.string().c_str());
    return 1;
  }
  std::vector<std::pair<std::string, std::string>> files;
  for (const auto& entry : fs::recursive_directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    const std::string name = entry.path().filename().string();
    if (name.empty() || name[0] == '.') continue;
    const std::ifstream in(entry.path(), std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    files.emplace_back(fs::relative(entry.path(), dir).generic_string(),
                       ss.str());
  }
  std::ranges::sort(files);
  const std::string bytes =
      mythos::vfs::BuildPack(files, mythos::vfs::KeyFromString(argv[3]));
  std::error_code ec;
  if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);
  std::ofstream o(out, std::ios::binary | std::ios::trunc);
  o.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!o) {
    std::fprintf(stderr, "mythos_pack: cannot write %s\n",
                 out.string().c_str());
    return 1;
  }
  std::printf("mythos_pack: %zu files, %zu bytes -> %s\n", files.size(),
              bytes.size(), out.string().c_str());
  return 0;
}
