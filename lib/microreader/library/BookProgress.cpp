#include "BookProgress.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace microreader {

namespace {

std::string clean_field(std::string_view s) {
  std::string out(s);
  for (char& c : out)
    if (c == '\t' || c == '\n' || c == '\r')
      c = ' ';
  return out;
}

}  // namespace

void BookProgress::load() {
  entries_.clear();
  dirty_ = false;
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
    if (n < 2 || fields[0][0] < '0' || fields[0][0] > '9' || fields[1][0] == '\0')
      continue;
    Entry e;
    const long pct = std::strtol(fields[0], nullptr, 10);
    e.percent = pct < 0 ? 0 : (pct > 100 ? 100 : static_cast<int>(pct));
    e.path = fields[1];
    if (n > 2)
      e.title = fields[2];
    if (n > 3)
      e.author = fields[3];
    const int existing = find_(e.path, e.title, e.author);
    if (existing >= 0)
      entries_[static_cast<size_t>(existing)] = std::move(e);  // later lines win
    else if (entries_.size() < kMaxBooks)
      entries_.push_back(std::move(e));
  }
  std::fclose(f);
}

bool BookProgress::save() {
  if (!dirty_ || file_.empty())
    return false;
  FILE* f = std::fopen(file_.c_str(), "w");
  if (!f)
    return false;
  std::fputs("# book progress v1: <percent>\t<path>\t<title>\t<author>\n", f);
  for (const auto& e : entries_)
    std::fprintf(f, "%d\t%s\t%s\t%s\n", e.percent, e.path.c_str(), e.title.c_str(), e.author.c_str());
  std::fclose(f);
  dirty_ = false;
  return true;
}

int BookProgress::find_(std::string_view path, std::string_view title, std::string_view author) const {
  for (size_t i = 0; i < entries_.size(); ++i) {
    const Entry& e = entries_[i];
    if (!path.empty() && e.path == path)
      return static_cast<int>(i);
    if (!title.empty() && e.title == title && e.author == author)
      return static_cast<int>(i);
  }
  return -1;
}

int BookProgress::get(std::string_view path, std::string_view title, std::string_view author) const {
  const int i = find_(path, title, author);
  return i < 0 ? -1 : entries_[static_cast<size_t>(i)].percent;
}

void BookProgress::set(std::string_view path, std::string_view title, std::string_view author, int percent) {
  if (percent < 0)
    percent = 0;
  if (percent > 100)
    percent = 100;
  const int i = find_(path, title, author);
  if (i >= 0) {
    Entry& e = entries_[static_cast<size_t>(i)];
    if (e.percent == percent && e.path == path)
      return;
    e.percent = percent;
    e.path = clean_field(path);
    dirty_ = true;
    return;
  }
  if (entries_.size() >= kMaxBooks)
    entries_.erase(entries_.begin());  // forget the oldest
  Entry e;
  e.path = clean_field(path);
  e.title = clean_field(title);
  e.author = clean_field(author);
  e.percent = percent;
  entries_.push_back(std::move(e));
  dirty_ = true;
}

}  // namespace microreader
