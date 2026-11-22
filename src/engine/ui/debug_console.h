#ifndef MYTHOS_ENGINE_UI_DEBUG_CONSOLE_H_
#define MYTHOS_ENGINE_UI_DEBUG_CONSOLE_H_

#include <deque>
#include <string>
#include <vector>

#include "engine/core/cvar.h"

// The state of the in-game debug console and of the frame-time graph: what is
// typed, what was printed, the command history. Headless; the platform feeds
// it key presses and `DrawDebugOverlay` draws it.
namespace mythos {
// The last frame times, for fps and a graph.
class FrameHistory {
 public:
  static constexpr int kCapacity = 240;

  // Adds a frame time in seconds; negative, NaN or huge (> 10 s) values are
  // ignored.
  void Add(double seconds);

  // Number of samples held (up to kCapacity).
  [[nodiscard]] int Count() const { return count_; }

  // The samples in milliseconds, oldest first.
  [[nodiscard]] std::vector<double> Milliseconds() const;

  // Average of the last `n` frames in ms (0 when none).
  [[nodiscard]] double AverageMs(int n = 60) const;

  // The slowest frame held, in ms.
  [[nodiscard]] double MaxMs() const;

  // Frames per second from `AverageMs` (0 when none).
  [[nodiscard]] double Fps(int n = 60) const;

 private:
  double samples_[kCapacity] = {};
  int next_ = 0;
  int count_ = 0;
};

class DebugConsole {
 public:
  static constexpr int kMaxLines = 200;
  static constexpr int kMaxHistory = 50;

  // `registry` runs the lines and completes names; it must outlive the console.
  explicit DebugConsole(CvarRegistry& registry) : reg_(&registry) {}

  [[nodiscard]] bool Visible() const { return visible_; }
  void Toggle() { visible_ = !visible_; }
  void Show() { visible_ = true; }
  void Hide() { visible_ = false; }

  // Appends typed text; control characters and the backtick that opens the
  // console are dropped. The line is limited to 200 bytes.
  void Type(const std::string& utf8);

  // Removes the last character (a whole UTF-8 character).
  void Backspace();

  // Runs the line: echoes it, prints the result, remembers it for the history
  // and clears the line.
  void Submit();

  // Steps through the earlier lines (up goes back); down past the newest
  // restores what was being typed.
  void HistoryUp();
  void HistoryDown();

  // Completes the first word: one match fills it in, several fill their common
  // start and list the matches.
  void Complete();

  // Prints a line (it may be several, split at '\n').
  void Log(const std::string& text);

  // Scrolls the printed lines back (positive) or forward (negative).
  void Scroll(int lines);

  // Clears the printed lines.
  void Clear();

  [[nodiscard]] const std::string& Input() const { return input_; }
  [[nodiscard]] const std::deque<std::string>& Lines() const { return lines_; }

  // How many lines the view is scrolled back (0 = the newest).
  [[nodiscard]] int ScrollBack() const { return scroll_; }

 private:
  CvarRegistry* reg_;
  bool visible_ = false;
  std::string input_;
  std::string draft_;  // what was being typed while browsing the history
  std::deque<std::string> lines_;
  std::vector<std::string> history_;
  int browse_ = -1;  // index into history_, -1 = not browsing
  int scroll_ = 0;
};
}  // namespace mythos

#endif
