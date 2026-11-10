#pragma once

// The "Finished" shelf: books the reader has marked as finished.
//
// Finished books are hidden from the main book list and shown on their own
// shelf (Settings → Finished Books). A book is marked or unmarked from its
// in-book menu.
//
// Storage: <data_dir>/finished.txt, one book per line:
//
//     <finished_at>\t<path>\t<title>\t<author>
//
// finished_at is local seconds since 1970 (0 = unknown). A book matches an
// entry by path, or by title + author, so renaming or moving the .epub file
// keeps it on the shelf.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace microreader {

struct FinishedBook {
  std::string path;
  std::string title;
  std::string author;
  int64_t finished_at = 0;
};

class FinishedBooks {
 public:
  // File to load from / save to. Empty = in-memory only.
  void set_file(std::string path) {
    file_ = std::move(path);
  }
  const std::string& file() const {
    return file_;
  }

  // Replaces the in-memory list with the file's contents (missing file = empty).
  void load();
  bool save() const;

  bool contains(std::string_view path, std::string_view title, std::string_view author) const;

  // Mark (finished=true) or unmark a book, then save. `now` is the local time
  // used as the finished date. Returns true if anything changed.
  bool set(std::string_view path, std::string_view title, std::string_view author, bool finished, int64_t now);

  // Newest first.
  const std::vector<FinishedBook>& books() const {
    return books_;
  }
  size_t size() const {
    return books_.size();
  }

  // Bumped on every change, so screens can tell when to refresh.
  uint64_t generation() const {
    return generation_;
  }

 private:
  int find_(std::string_view path, std::string_view title, std::string_view author) const;
  void sort_();

  std::string file_;
  std::vector<FinishedBook> books_;
  uint64_t generation_ = 0;
};

}  // namespace microreader
