#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "StringPool.h"

namespace microreader {

class DrawBuffer;

struct BookIndexEntry {
  StringRef path{};
  StringRef title{};
  StringRef author{};
  uint32_t last_open_order = 0;  // 0 = never opened; higher = more recently opened
};

static constexpr int MAX_BOOKS = 250;

class BookIndex {
 public:
  static BookIndex& instance();

  bool load(const std::string& index_file);
  bool save(const std::string& index_file) const;

  // Scan a directory recursively, updating the index
  // Uses DrawBuffer for scratch buffers and for showing loading progress.
  void build_index(const std::string& root_dir, DrawBuffer& buf);

  const std::vector<BookIndexEntry>& entries() const {
    return entries_;
  }

  const StringPool& pool() const {
    return pool_;
  }

  // Add an entry; uses the pool to store strings.
  void add_entry(std::string_view path, std::string_view title, std::string_view author, uint32_t last_open_order = 0);

  // Record that a book was opened. `order` should be a monotonically
  // increasing counter (higher = more recently opened). Updates the in-memory
  // entry only; call save() afterwards to persist.
  void set_last_opened(std::string_view path, uint32_t order);

  // Remove the entry at `index` from the in-memory entries vector.
  // The StringPool is not compacted (individual strings cannot be freed).
  void remove_entry(int index);

  void clear_entries();

 private:
  std::vector<BookIndexEntry> entries_;
  StringPool pool_;
  BookIndex() = default;
};

}  // namespace microreader
