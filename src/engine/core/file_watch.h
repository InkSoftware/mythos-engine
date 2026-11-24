// Polls files and folders for changes (by modification time and size), for
// reloading assets while a game runs. Development use: it scans the file
// system, so poll a couple of times a second, not every frame (the interval
// does that). No threads, no OS notification APIs: it behaves the same on every
// platform.
#ifndef MYTHOS_ENGINE_CORE_FILE_WATCH_H_
#define MYTHOS_ENGINE_CORE_FILE_WATCH_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mythos {
class FileWatcher {
 public:
  // `interval_seconds` is the least time between two scans.
  explicit FileWatcher(double interval_seconds = 0.5)
      : interval_(interval_seconds) {}

  // Watches a file, or every file under a folder (recursively). What is there
  // now is the baseline: it is not reported as a change. Returns false when
  // the path does not exist.
  bool Add(const std::string& path);

  void Clear();

  // Advances the clock by `dt` seconds and, when the interval has passed,
  // scans. Returns the paths that were added, changed or removed since the
  // last scan, sorted; empty between scans.
  std::vector<std::string> Poll(double dt);

 private:
  struct Stamp {
    int64_t time = 0;
    uintmax_t size = 0;
    bool operator==(const Stamp&) const = default;
  };
  using Snapshot = std::map<std::string, Stamp>;
  static void Scan(const std::string& root, Snapshot& out);

  double interval_;
  double waited_ = 0;
  std::vector<std::string> roots_;
  Snapshot last_;
};
}

#endif
