#pragma once

#include <cstring>

#include "../Input.h"
#include "../display/DrawBuffer.h"
#include "ListMenuScreen.h"

namespace microreader {

enum class BookListFormat { TitleOnly, TitleAndAuthor, Filename };

// Main screen — lists EPUB books from a directory.
// Button1 = open book, Button0 = settings.
class MainMenu final : public ListMenuScreen {
 public:
  MainMenu() = default;

  void set_books_dir(const char* dir) {
    books_dir_ = dir;
  }

  // Force the book list to rescan on the next update.
  void request_rescan() {
    needs_scan_ = true;
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

  // The full path of the currently highlighted entry (even if not yet opened).
  const std::string& current_book_path() const {
    int idx = selected();
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

  void set_app(Application* app) {
    app_ = app;
  }

  void start(DrawBuffer& buf, IRuntime& runtime) override {
    buf_ = &buf;
    ListMenuScreen::start(buf, runtime);
  }

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
  bool needs_scan_ = false;

  // Long-press delete state
  int pending_select_idx_ = -1;   // index selected via on_select, pending resolution
  int delete_index_ = -1;         // book to delete when confirmed
  bool delete_pending_ = false;   // waiting for confirm/cancel on delete prompt
  uint32_t button1_hold_ms_ = 0;  // accumulated hold time for Button1
  std::string delete_book_label_; // original label of book pending deletion
  static constexpr uint32_t kLongPressMs = 800;

  void do_delete_(DrawBuffer& buf, IRuntime& runtime);
  void cancel_delete_(DrawBuffer& buf, IRuntime& runtime);

  struct BookEntry {
    std::string path;
    std::string label;
  };
  std::vector<BookEntry> entries_;

  void scan_directory_(DrawBuffer& buf);
  void populate_list_();
};

}  // namespace microreader
