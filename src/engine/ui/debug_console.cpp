#include "engine/ui/debug_console.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace mythos {
namespace {
constexpr size_t kMaxInput = 200;
}

void FrameHistory::Add(const double seconds) {
  if (!std::isfinite(seconds) || seconds < 0.0 || seconds > 10.0) return;
  samples_[next_] = seconds * 1000.0;
  next_ = (next_ + 1) % kCapacity;
  count_ = std::min(count_ + 1, kCapacity);
}

std::vector<double> FrameHistory::Milliseconds() const {
  std::vector<double> out;
  out.reserve(static_cast<size_t>(count_));
  const int start = (next_ - count_ + kCapacity) % kCapacity;
  for (int i = 0; i < count_; ++i) {
    out.push_back(samples_[(start + i) % kCapacity]);
  }
  return out;
}

double FrameHistory::AverageMs(const int n) const {
  const int take = std::min(std::max(n, 0), count_);
  if (take == 0) return 0.0;
  double sum = 0.0;
  for (int i = 1; i <= take; ++i) {
    sum += samples_[(next_ - i + kCapacity) % kCapacity];
  }
  return sum / take;
}

double FrameHistory::MaxMs() const {
  double top = 0.0;
  for (int i = 0; i < count_; ++i) top = std::max(top, samples_[i]);
  return top;
}

double FrameHistory::Fps(const int n) const {
  const double ms = AverageMs(n);
  return ms > 0.0 ? 1000.0 / ms : 0.0;
}

void DebugConsole::Type(const std::string& utf8) {
  for (const char c : utf8) {
    const auto u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7F || c == '`') continue;
    if (input_.size() >= kMaxInput) break;
    input_ += c;
  }
  browse_ = -1;
}

void DebugConsole::Backspace() {
  if (input_.empty()) return;
  // Drop continuation bytes, then the lead byte.
  while (!input_.empty() &&
         (static_cast<unsigned char>(input_.back()) & 0xC0) == 0x80) {
    input_.pop_back();
  }
  if (!input_.empty()) input_.pop_back();
  browse_ = -1;
}

void DebugConsole::Log(const std::string& text) {
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = text.find('\n', start);
    const std::string line =
        text.substr(start, end == std::string::npos ? end : end - start);
    lines_.push_back(line);
    if (end == std::string::npos) break;
    start = end + 1;
  }
  while (lines_.size() > static_cast<size_t>(kMaxLines)) lines_.pop_front();
  scroll_ = 0;
}

void DebugConsole::Submit() {
  const std::string line = input_;
  input_.clear();
  draft_.clear();
  browse_ = -1;
  if (line.find_first_not_of(' ') == std::string::npos) return;
  Log("> " + line);
  if (history_.empty() || history_.back() != line) history_.push_back(line);
  if (history_.size() > static_cast<size_t>(kMaxHistory)) {
    history_.erase(history_.begin());
  }
  const CvarResult r = reg_->Execute(line);
  if (!r.text.empty()) Log(r.ok ? r.text : "error: " + r.text);
}

void DebugConsole::HistoryUp() {
  if (history_.empty()) return;
  if (browse_ < 0) {
    draft_ = input_;
    browse_ = static_cast<int>(history_.size()) - 1;
  } else if (browse_ > 0) {
    --browse_;
  }
  input_ = history_[static_cast<size_t>(browse_)];
}

void DebugConsole::HistoryDown() {
  if (browse_ < 0) return;
  if (browse_ + 1 < static_cast<int>(history_.size())) {
    ++browse_;
    input_ = history_[static_cast<size_t>(browse_)];
  } else {
    browse_ = -1;
    input_ = draft_;
  }
}

void DebugConsole::Complete() {
  // Only the first word (the name) is completed.
  if (input_.find(' ') != std::string::npos) return;
  const std::vector<std::string> matches = reg_->Complete(input_);
  if (matches.empty()) return;
  if (matches.size() == 1) {
    input_ = matches[0] + " ";
    return;
  }
  // The longest start they share, ignoring case.
  std::string common = matches[0];
  for (const std::string& m : matches) {
    size_t n = 0;
    while (n < common.size() && n < m.size() &&
           std::tolower(static_cast<unsigned char>(common[n])) ==
               std::tolower(static_cast<unsigned char>(m[n]))) {
      ++n;
    }
    common.resize(n);
  }
  if (common.size() > input_.size()) input_ = common;
  std::string list;
  for (const std::string& m : matches) list += (list.empty() ? "" : "  ") + m;
  Log(list);
}

void DebugConsole::Scroll(const int lines) {
  const int top = std::max(0, static_cast<int>(lines_.size()) - 1);
  scroll_ = std::clamp(scroll_ + lines, 0, top);
}

void DebugConsole::Clear() {
  lines_.clear();
  scroll_ = 0;
}
}  // namespace mythos
