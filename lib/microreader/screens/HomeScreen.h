#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../Input.h"
#include "../display/DrawBuffer.h"
#include "IScreen.h"

namespace microreader {

// Cover home page (Settings → Home Screen: Covers).
//
//                Sami
//
//          Finished Books (N)
//
//     [cover]   [cover]   [cover]
//       42%       10%       88%
//        Title of the selected book
//
//              Library
//
// The three covers are the books opened most recently (finished books are
// left out). ▲/▼ move through the row: Finished Books, the covers, Library;
// ▶ opens the selection, ◀ opens Settings. The cursor starts on the first
// cover.
//
// Covers are decoded once per book and screen size and kept as small 1-bit
// thumbnails in <data_dir>/cache/<book>/thumb-<w>x<h>.bin, so the page draws
// quickly afterwards.
class HomeScreen final : public IScreen {
 public:
  static constexpr int kMaxBooks = 3;

  const char* name() const override {
    return "Home";
  }

  void start(DrawBuffer& buf, IRuntime& runtime) override;
  void stop() override;
  void update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;

  // Item indices: 0 = Finished Books, 1..books = covers, books+1 = Library.
  int selected() const {
    return selected_;
  }
  int book_count() const {
    return static_cast<int>(books_.size());
  }

  // Thumbnail cache file for a book at a given box size (exposed for tests).
  static std::string thumb_path(const char* data_dir, const std::string& book_path, int box_w, int box_h);

 private:
  struct RecentBook {
    std::string path;
    std::string title;
    std::string author;
    int percent = -1;
  };

  // Where the cursor returns to after a child screen closes.
  enum class Return : uint8_t { FirstBook, Finished, Library };

  struct Layout {
    int header_h = 0;
    int finished_y = 0;  // top of the Finished Books link
    int covers_y = 0;    // top of the cover row
    int cover_w = 0;
    int cover_h = 0;
    int slot_x[kMaxBooks] = {};
    int percent_y = 0;  // top of the percent row
    int caption_y = 0;  // top of the selected book's title
    int library_y = 0;  // top of the Library link
  };

  std::vector<RecentBook> books_;
  int selected_ = 1;
  Return return_to_ = Return::FirstBook;
  std::string first_book_;  // first cover when the page was left
  bool needs_scan_ = false;
  bool needs_thumbs_ = false;
  uint64_t cached_generation_ = 0;

  int count_() const {
    return static_cast<int>(books_.size()) + 2;
  }
  int library_index_() const {
    return static_cast<int>(books_.size()) + 1;
  }

  void load_books_();
  void scan_(DrawBuffer& buf);
  Layout layout_(const DrawBuffer& buf) const;
  bool thumbs_missing_(const DrawBuffer& buf) const;
  void make_thumbs_(DrawBuffer& buf);
  void draw_(DrawBuffer& buf, std::optional<uint8_t> battery_pct) const;
  void draw_cover_(DrawBuffer& buf, const Layout& L, int slot) const;
  void open_selected_();
};

}  // namespace microreader
