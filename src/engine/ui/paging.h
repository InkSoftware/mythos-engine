#ifndef MYTHOS_ENGINE_UI_PAGING_H_
#define MYTHOS_ENGINE_UI_PAGING_H_
// Paging and wrapping for any game's notebook, documents and credits:
// headless, pure helpers. Text that is longer than one screen is split at page
// markers, paragraphs, sentences and words (never inside a UTF-8 sequence);
// entries are dealt onto pages by their measured height.

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "engine/gfx/text.h"

namespace mythos {

// An author's forced page break inside a text: a form feed ("\f" in JSON) or
// this token.
inline constexpr const char* kPageToken = "[page]";

// Splits `text` into pages. A page break is kPageToken or '\f'; otherwise a
// page takes as much as `fits` accepts (it says whether a candidate page is
// short enough), breaking after a line (\n), a sentence or a word, and only
// inside a word when one word alone is too long. Pages are trimmed; there is
// always at least one (possibly empty).
std::vector<std::string> PaginateText(
    const std::string& text,
    const std::function<bool(const std::string&)>& fits);

// The same with a plain limit of `max_chars` UTF-8 characters (codepoints) a
// page.
std::vector<std::string> PaginateText(const std::string& text,
                                      size_t max_chars);

// Wraps text to `width` at `size`, honouring '\n' as a line break (a blank
// line stays a blank line).
std::vector<std::string> WrapLines(const SdfFont& font, float size, float width,
                                   const std::string& text);

// Breaks `text` at spaces into lines of at most `max_chars` UTF-8 characters (a
// word longer than a line stays whole). Always at least one line.
std::vector<std::string> WrapWords(const std::string& text, size_t max_chars);

// Deals entries of the given heights onto pages at most `max_height` tall
// (`gap` after each); returns the index of each page's first entry. An entry
// taller than a page has a page of its own. Empty input gives one page at 0.
std::vector<int> PaginateEntries(const std::vector<float>& heights,
                                 float max_height, float gap);

// `page` kept inside 0 .. count - 1 (0 when count < 1).
int ClampPage(int page, int count);

}  // namespace mythos

#endif  // MYTHOS_ENGINE_UI_PAGING_H_
