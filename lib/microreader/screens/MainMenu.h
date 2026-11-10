#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

#include "../Input.h"
#include "../content/StringPool.h"
#include "../display/DrawBuffer.h"
#include "ListMenuScreen.h"

namespace microreader {

// Values are stored in the settings file (list_format=): only append.
enum class BookListFormat { TitleOnly, TitleAndAuthor, Filename, TitleAndPercent };
enum class BookSortOrder { Alphabetical, LastOpened };

// Main screen — lists EPUB books from a directory.
// Button1 = open book, Button0 = settings.
// The first row, "Finished Books (N)", opens the Finished shelf; a gap row
// separates it from the books.
class MainMenu final : public ListMenuScreen {
 public:
  MainMenu() = default;

  void set_books_dir(const char* dir) {
    books_dir_ = dir;
  }

  // Restore the book list selection to the entry matching this path.
  // Call before start(); applied after directory scan.
  void set_initial_selection(const char* path) {
    initial_selection_ = path ? path : "";
  }

  // The full path of the most recently selected (opened) book.
  const std::string& last_selected_book_path() const {
    return last_selected_path_;
  }

  // Number of books on the list (excluding the Finished row and gaps).
  int book_count() const {
    return static_cast<int>(entries_.size());
  }

  // The full path of the currently highlighted entry (even if not yet opened).
  // Empty when the Finished row is highlighted.
  const std::string& current_book_path() const {
    int idx = entries_index_for(selected());
    if (idx >= 0 && idx < static_cast<int>(entries_.size()))
      return entries_[idx].path;
    static const std::string kEmpty;
    return kEmpty;
  }

  bool has_books_dir() const {
    return books_dir_ != nullptr;
  }

  const char* books_dir() const {
    return books_dir_;
  }

  const char* name() const override {
    return "Books";
  }

  BookListFormat list_format() const {
    return list_format_;
  }
  void set_list_format(BookListFormat format) {
    list_format_ = format;
  }

  BookSortOrder sort_order() const {
    return sort_order_;
  }
  void set_sort_order(BookSortOrder order) {
    sort_order_ = order;
  }

  void set_app(Application* app) {
    app_ = app;
  }

  std::string_view get_item_label(int index) const override;
  bool is_separator(int index) const override;
  int count() const override;

  void start(DrawBuffer& buf, IRuntime& runtime) override {
    buf_ = &buf;
    ListMenuScreen::start(buf, runtime);
  }

  void stop() override;

  void update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;

 protected:
  void on_start() override;
  void on_select(int index) override;
  void on_back() override;

 private:
  const char* books_dir_ = nullptr;
  std::string initial_selection_;   // path to pre-select after scan
  std::string last_selected_path_;  // path of the most recently opened book
  DrawBuffer* buf_ = nullptr;
  BookListFormat list_format_ = BookListFormat::TitleOnly;
  BookSortOrder sort_order_ = BookSortOrder::Alphabetical;
  bool needs_scan_ = false;
  // Cached BookIndex::generation() value from the last populate_list_(). When
  // update() detects a mismatch, the index was mutated externally (e.g. by a
  // serial upload/delete/rename while this screen is showing) and we refresh
  // the list in place without requiring the user to navigate away and back.
  uint64_t cached_generation_ = 0;

  struct BookEntry {
    std::string path;
    StringRef title_ref;
    StringRef author_ref;
    uint32_t last_open_order = 0;
  };
  std::vector<BookEntry> entries_;
  mutable std::string label_buf_;
  mutable std::string finished_label_;
  // Index (among the book rows) of the "opened / never opened" gap, or -1.
  int separator_visual_index_ = -1;
  // The Finished row was highlighted, to return to it; -1 if not.
  int return_to_row_ = -1;

 public:
  // Rows above the books: "Finished Books (N)" and a gap.
  static constexpr int kTopRows = 2;
  static constexpr int kFinishedRow = 0;

 private:
  // Visual list index → entries_ index (-1 for the top rows and gaps).
  int entries_index_for(int visual) const {
    const int v = visual - kTopRows;
    if (v < 0) return -1;
    if (separator_visual_index_ < 0 || v < separator_visual_index_) return v;
    if (v == separator_visual_index_) return -1;
    return v - 1;
  }

  int visual_for_entries(int real) const {
    if (separator_visual_index_ < 0 || real < separator_visual_index_) return real + kTopRows;
    return real + 1 + kTopRows;
  }

  // Title + " · N%" fitted to the row width (the title is shortened, never the percent).
  std::string_view title_with_percent_(std::string_view title, int percent) const;

  void scan_directory_(DrawBuffer& buf);
  void populate_list_();
};

}  // namespace microreader
