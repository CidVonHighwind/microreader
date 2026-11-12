#include "FinishedBooks.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace microreader {

namespace {

// Tabs and line breaks would break the line format.
std::string clean_field(std::string_view s) {
  std::string out(s);
  for (char& c : out)
    if (c == '\t' || c == '\n' || c == '\r')
      c = ' ';
  return out;
}

}  // namespace

void FinishedBooks::load() {
  books_.clear();
  ++generation_;
  if (file_.empty())
    return;
  FILE* f = std::fopen(file_.c_str(), "r");
  if (!f)
    return;
  char line[768];
  while (std::fgets(line, sizeof(line), f)) {
    char* nl = std::strchr(line, '\n');
    if (!nl && !std::feof(f)) {  // over-long line: skip it entirely
      int c;
      while ((c = std::fgetc(f)) != EOF && c != '\n') {
      }
      continue;
    }
    if (nl)
      *nl = '\0';
    if (char* cr = std::strchr(line, '\r'))
      *cr = '\0';
    if (line[0] == '#' || line[0] == '\0')
      continue;

    // Split into exactly four tab-separated fields.
    char* fields[4] = {};
    char* p = line;
    int n = 0;
    while (n < 4) {
      fields[n++] = p;
      char* tab = std::strchr(p, '\t');
      if (!tab)
        break;
      *tab = '\0';
      p = tab + 1;
    }
    if (n < 2 || fields[1][0] == '\0')
      continue;  // need at least a date and a path
    FinishedBook b;
    b.finished_at = static_cast<int64_t>(std::strtoll(fields[0], nullptr, 10));
    b.path = fields[1];
    if (n > 2)
      b.title = fields[2];
    if (n > 3)
      b.author = fields[3];
    if (find_(b.path, b.title, b.author) < 0 && books_.size() < 1000)
      books_.push_back(std::move(b));
  }
  std::fclose(f);
  sort_();
}

bool FinishedBooks::save() const {
  if (file_.empty())
    return false;
  FILE* f = std::fopen(file_.c_str(), "w");
  if (!f)
    return false;
  std::fputs("# finished books v1: <finished_at>\t<path>\t<title>\t<author>\n", f);
  // %lu, not %lld: the firmware uses newlib-nano printf, which has no 64-bit
  // formats. 32-bit unsigned seconds last until 2106.
  for (const auto& b : books_)
    std::fprintf(f, "%lu\t%s\t%s\t%s\n", static_cast<unsigned long>(b.finished_at > 0 ? b.finished_at : 0),
                 b.path.c_str(), b.title.c_str(), b.author.c_str());
  std::fclose(f);
  return true;
}

int FinishedBooks::find_(std::string_view path, std::string_view title, std::string_view author) const {
  for (size_t i = 0; i < books_.size(); ++i) {
    const FinishedBook& b = books_[i];
    if (!path.empty() && b.path == path)
      return static_cast<int>(i);
    if (!title.empty() && b.title == title && b.author == author)
      return static_cast<int>(i);
  }
  return -1;
}

bool FinishedBooks::contains(std::string_view path, std::string_view title, std::string_view author) const {
  return find_(path, title, author) >= 0;
}

bool FinishedBooks::set(std::string_view path, std::string_view title, std::string_view author, bool finished,
                        int64_t now) {
  const int idx = find_(path, title, author);
  if (finished) {
    if (idx >= 0) {
      // Already finished: keep the original date, but refresh a moved path.
      FinishedBook& b = books_[static_cast<size_t>(idx)];
      if (b.path == path)
        return false;
      b.path = clean_field(path);
    } else {
      FinishedBook b;
      b.path = clean_field(path);
      b.title = clean_field(title);
      b.author = clean_field(author);
      b.finished_at = now;
      books_.push_back(std::move(b));
      sort_();
    }
  } else {
    if (idx < 0)
      return false;
    books_.erase(books_.begin() + idx);
  }
  ++generation_;
  save();
  return true;
}

void FinishedBooks::sort_() {
  std::stable_sort(books_.begin(), books_.end(),
                   [](const FinishedBook& a, const FinishedBook& b) { return a.finished_at > b.finished_at; });
}

}  // namespace microreader
