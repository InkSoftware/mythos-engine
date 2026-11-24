#include "engine/core/file_watch.h"

#include <algorithm>
#include <filesystem>

namespace mythos {
namespace {
namespace fs = std::filesystem;
}

void FileWatcher::Scan(const std::string& root, Snapshot& out) {
  std::error_code ec;
  const auto stamp = [&](const fs::path& p) {
    Stamp s;
    const auto t = fs::last_write_time(p, ec);
    s.time = ec ? 0 : static_cast<int64_t>(t.time_since_epoch().count());
    s.size = fs::file_size(p, ec);
    if (ec) s.size = 0;
    return s;
  };
  if (fs::is_regular_file(root, ec)) {
    out[root] = stamp(root);
  } else if (fs::is_directory(root, ec)) {
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end;
         it.increment(ec)) {
      if (it->is_regular_file(ec)) out[it->path().string()] = stamp(it->path());
    }
  }
}

bool FileWatcher::Add(const std::string& path) {
  std::error_code ec;
  if (!fs::exists(path, ec)) return false;
  roots_.push_back(path);
  Scan(path, last_);
  return true;
}

void FileWatcher::Clear() {
  roots_.clear();
  last_.clear();
  waited_ = 0;
}

std::vector<std::string> FileWatcher::Poll(const double dt) {
  waited_ += dt;
  if (waited_ < interval_) return {};
  waited_ = 0;
  Snapshot now;
  for (const std::string& root : roots_) Scan(root, now);
  std::vector<std::string> changed;
  for (const auto& [path, stamp] : now) {
    const auto it = last_.find(path);
    if (it == last_.end() || !(it->second == stamp)) changed.push_back(path);
  }
  for (const auto& entry : last_) {
    if (!now.contains(entry.first)) changed.push_back(entry.first);
  }
  last_ = std::move(now);
  std::ranges::sort(changed);
  return changed;
}
}
