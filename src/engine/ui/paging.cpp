#include "engine/ui/paging.h"

#include <algorithm>
#include <cstddef>
#include <sstream>

namespace mythos {

namespace {

bool IsSpace(char c) {
  return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f';
}

std::string Trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && IsSpace(s[a])) ++a;
  while (b > a && IsSpace(s[b - 1])) --b;
  return s.substr(a, b - a);
}

// The length of the UTF-8 sequence starting with lead byte c (1 for a stray
// byte).
size_t SequenceLength(unsigned char c) {
  if (c >= 0xF0) return 4;
  if (c >= 0xE0) return 3;
  if (c >= 0xC0) return 2;
  return 1;
}

bool StartsAt(const std::string& s, size_t i, const char* token) {
  return s.compare(i, std::char_traits<char>::length(token), token) == 0;
}

// The text up to each forced page break.
std::vector<std::string> SplitAtBreaks(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  const size_t token_len = std::char_traits<char>::length(kPageToken);
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '\f') {
      out.push_back(cur);
      cur.clear();
      ++i;
    } else if (StartsAt(text, i, kPageToken)) {
      out.push_back(cur);
      cur.clear();
      i += token_len;
    } else {
      cur += text[i++];
    }
  }
  out.push_back(cur);
  return out;
}

// Whether a sentence ends after s[i] (a full stop, !, ? or an ellipsis
// followed by a space or the end).
bool SentenceEnd(const std::string& s, size_t i) {
  const char c = s[i];
  if (c != '.' && c != '!' && c != '?') {
    // U+2026 (...) is E2 80 A6; its last byte ends one.
    if (!(i >= 2 && static_cast<unsigned char>(s[i]) == 0xA6 &&
          static_cast<unsigned char>(s[i - 1]) == 0x80 &&
          static_cast<unsigned char>(s[i - 2]) == 0xE2))
      return false;
  }
  return i + 1 >= s.size() || IsSpace(s[i + 1]);
}

// Levels, coarse to fine: lines, sentences, words, codepoints. Each piece keeps
// its trailing separator, so joining the pieces gives the text back.
std::vector<std::string> Pieces(const std::string& s, int level) {
  std::vector<std::string> out;
  std::string cur;
  for (size_t i = 0; i < s.size();) {
    if (level == 3) {
      const size_t n = std::min(
          SequenceLength(static_cast<unsigned char>(s[i])), s.size() - i);
      out.push_back(s.substr(i, n));
      i += n;
      continue;
    }
    cur += s[i];
    bool cut = false;
    if (level == 0) cut = s[i] == '\n';
    if (level == 1)
      cut = s[i] == '\n' || (IsSpace(s[i]) && i > 0 && SentenceEnd(s, i - 1));
    if (level == 2) cut = IsSpace(s[i]);
    ++i;
    if (cut) {
      // Keep a run of spaces with the piece before it.
      while (level != 0 && i < s.size() && IsSpace(s[i]) && s[i] != '\n')
        cur += s[i++];
      out.push_back(cur);
      cur.clear();
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

struct Packer {
  const std::function<bool(const std::string&)>& fits;
  std::vector<std::string> pages;
  std::string cur;

  void Flush() {
    const std::string t = Trim(cur);
    if (!t.empty()) pages.push_back(t);
    cur.clear();
  }
  bool Fits(const std::string& s) const { return fits(Trim(s)); }

  void Add(const std::string& s, int level) {
    for (const std::string& p : Pieces(s, level)) {
      if (Fits(cur + p)) {
        cur += p;
        continue;
      }
      if (!Trim(cur).empty()) {
        Flush();
        if (Fits(p)) {
          cur = p;
          continue;
        }
      }
      cur.clear();
      if (level >= 3) {  // one character that does not fit: take it anyway
        cur = p;
      } else {
        Add(p, level + 1);
      }
    }
  }
};

}  // namespace

std::vector<std::string> PaginateText(
    const std::string& text,
    const std::function<bool(const std::string&)>& fits) {
  std::vector<std::string> pages;
  for (const std::string& segment : SplitAtBreaks(text)) {
    Packer p{fits, {}, {}};
    p.Add(segment, 0);
    p.Flush();
    for (auto& s : p.pages) pages.push_back(std::move(s));
  }
  if (pages.empty()) pages.emplace_back();
  return pages;
}

std::vector<std::string> PaginateText(const std::string& text,
                                      size_t max_chars) {
  return PaginateText(text, [max_chars](const std::string& s) {
    size_t n = 0;
    for (const char c : s)
      if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n <= max_chars;
  });
}

std::vector<std::string> WrapLines(const mythos::SdfFont& font, float size,
                                   float width, const std::string& text) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = text.find('\n', start);
    const std::string line = text.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    std::vector<std::string> wrapped = font.Wrap(line, size, width);
    if (wrapped.empty()) wrapped.emplace_back();
    for (auto& w : wrapped) out.push_back(std::move(w));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return out;
}

std::vector<int> PaginateEntries(const std::vector<float>& heights,
                                 float max_height, float gap) {
  std::vector<int> starts{0};
  float used = 0;
  for (size_t i = 0; i < heights.size(); ++i) {
    const float h = heights[i] + gap;
    if (used > 0 && used + h > max_height + gap) {
      starts.push_back(static_cast<int>(i));
      used = 0;
    }
    used += h;
  }
  return starts;
}

int ClampPage(int page, int count) {
  return std::max(0, std::min(page, count - 1));
}

std::vector<std::string> WrapWords(const std::string& text, size_t max_chars) {
  const auto length = [](const std::string& str) {
    size_t n = 0;
    for (const unsigned char ch : str) n += (ch & 0xC0) != 0x80;
    return n;
  };
  std::vector<std::string> lines;
  std::istringstream words(text);
  std::string word, line;
  while (words >> word) {
    if (!line.empty() && length(line) + 1 + length(word) > max_chars) {
      lines.push_back(line);
      line.clear();
    }
    line += (line.empty() ? "" : " ") + word;
  }
  if (!line.empty() || lines.empty()) lines.push_back(line);
  return lines;
}

}  // namespace mythos
