#pragma once

// How far through each book the reader is, for the "Title & Percent Read"
// book list (Settings → Book List).
//
// Updated in memory as pages turn; written to <data_dir>/progress.txt by
// save() (the app calls it whenever it saves its settings, e.g. when a book
// is closed or the device sleeps). One book per line:
//
//     <percent>\t<path>\t<title>\t<author>
//
// Like the Finished shelf, a book matches by path or by title + author, so a
// renamed file keeps its percentage.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace microreader {

class BookProgress {
 public:
  static constexpr size_t kMaxBooks = 500;

  void set_file(std::string path) {
    file_ = std::move(path);
  }

  // Replaces the in-memory list with the file's contents (missing file = empty).
  void load();
  // Writes the file if anything changed since the last load/save.
  bool save();

  // Percent read (0-100), or -1 if the book has never been opened.
  int get(std::string_view path, std::string_view title, std::string_view author) const;
  void set(std::string_view path, std::string_view title, std::string_view author, int percent);

  size_t size() const {
    return entries_.size();
  }
  bool dirty() const {
    return dirty_;
  }

 private:
  struct Entry {
    std::string path;
    std::string title;
    std::string author;
    int percent = 0;
  };
  int find_(std::string_view path, std::string_view title, std::string_view author) const;

  std::string file_;
  std::vector<Entry> entries_;  // most recently updated last
  bool dirty_ = false;
};

}  // namespace microreader
