#ifndef MYTHOS_ENGINE_UI_PAGING_H_
#define MYTHOS_ENGINE_UI_PAGING_H_

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "engine/gfx/text.h"

namespace mythos {
// Text for a book or note page set: a form feed or this token forces a page
// break.
inline constexpr const char* kPageToken = "[page]";

// Splits `text` into pages for which `fits` returns true. Breaks are made at
// the coarsest boundary that works (lines, then sentences, then words, then
// single UTF-8 characters) and whitespace at the ends of a page is trimmed.
// Explicit breaks (`kPageToken` or a form feed) always start a new page. Always
// returns at least one page, which is empty for empty text.
std::vector<std::string> PaginateText(
    const std::string& text,
    const std::function<bool(const std::string&)>& fits);

// Same, with a page limited to `max_chars` characters (UTF-8 code points, not
// bytes).
std::vector<std::string> PaginateText(const std::string& text,
                                      size_t max_chars);

// Wraps `text` to `width` using the font's metrics at `size`, keeping the
// existing newlines (an empty line stays an empty line).
std::vector<std::string> WrapLines(const SdfFont& font, float size, float width,
                                   const std::string& text);

// Greedy word wrap to `max_chars` characters (UTF-8 code points) per line,
// collapsing whitespace. A word longer than the limit gets its own line.
// Returns one empty line for empty text.
std::vector<std::string> WrapWords(const std::string& text, size_t max_chars);

// Groups entries of the given `heights` into pages of at most `max_height`,
// with `gap` between entries. Returns the index of the first entry of each
// page; an entry taller than a page gets a page of its own.
std::vector<int> PaginateEntries(const std::vector<float>& heights,
                                 float max_height, float gap);

// Clamps a page number into [0, count - 1] (0 when `count` is 0 or less).
int ClampPage(int page, int count);
}

#endif
