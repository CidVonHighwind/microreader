// Cover home page (Settings → Home Screen: Covers), the Font Size setting and
// the new built-in fonts in Settings. Driven through the real Application
// with button presses, like a user would.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "AppHarness.h"
#include "microreader/content/BookIndex.h"

namespace fs = std::filesystem;
using namespace microreader;
using namespace app_harness;

namespace {

class HomeScreenTest : public AppFixture {
 protected:
  void SetUp() override {
    AppFixture::SetUp();
    covers_home_ = true;
    // Alphabetical by title in the Library: Basic, Cover Book, Cover Book 3, Multi.
    use_books({"basic.epub", "with_cover.epub", "with_cover_epub3.epub", "multi_chapter.epub"});
  }

  const HomeScreen& home() const {
    return *app_->home_screen();
  }
  int library_row() const {
    return home().book_count() + 1;
  }

  // From the home page: select Library (▲ wraps from the first row).
  void go_library() {
    ASSERT_EQ(top(), "Home");
    for (int guard = 0; guard < 8 && home().selected() != library_row(); ++guard)
      tap(Button::Button3);
    ASSERT_EQ(home().selected(), library_row());
    tap(Button::Button1);
    ASSERT_EQ(top(), "Books");
  }

  // Open a book from the Library, turn a page, close it and go back to the
  // home page.
  void read_from_library(const std::string& name) {
    go_library();
    const std::string path = book_path(name);
    for (int guard = 0; guard < 12 && app_->main_menu()->current_book_path() != path; ++guard)
      tap(Button::Button2);
    ASSERT_EQ(app_->main_menu()->current_book_path(), path);
    tap(Button::Button1);
    ASSERT_EQ(top(), "Reader");
    tick();
    tap(Button::Button2);  // next page
    tap(Button::Button0);  // reader → Library
    ASSERT_EQ(top(), "Books");
    tap(Button::Button0);  // Library → home page
    ASSERT_EQ(top(), "Home");
    tick();  // covers are prepared on the first update
  }

  void select_settings_row(int row) {
    ASSERT_EQ(top(), "Settings");
    for (int guard = 0; guard < 40 && app_->settings()->selected_index() != row; ++guard)
      tap(Button::Button2);
    ASSERT_EQ(app_->settings()->selected_index(), row);
  }

  std::vector<fs::path> thumbs() const {
    std::vector<fs::path> out;
    for (auto it = fs::recursive_directory_iterator(data_dir_ / "cache"); it != fs::recursive_directory_iterator();
         ++it)
      if (it->path().filename().string().rfind("thumb-", 0) == 0)
        out.push_back(it->path());
    return out;
  }
};

}  // namespace

TEST_F(HomeScreenTest, StartsOnTheHomePageWithLibrarySelectedWhenNothingWasRead) {
  start_app();
  EXPECT_EQ(top(), "Home");
  EXPECT_EQ(home().book_count(), 0);
  EXPECT_EQ(home().selected(), library_row());
}

TEST_F(HomeScreenTest, RecentBooksNewestFirstUpToThreeWithFirstSelected) {
  start_app();
  read_from_library("basic.epub");
  EXPECT_EQ(home().book_count(), 1);
  EXPECT_EQ(home().selected(), 1) << "back from reading: the first cover is selected";

  read_from_library("with_cover.epub");
  read_from_library("with_cover_epub3.epub");
  read_from_library("multi_chapter.epub");
  EXPECT_EQ(home().book_count(), HomeScreen::kMaxBooks);
  EXPECT_EQ(home().selected(), 1);

  // ▶ on the second cover opens that book; afterwards it's the first cover.
  tap(Button::Button2);
  ASSERT_EQ(home().selected(), 2);
  tap(Button::Button1);
  ASSERT_EQ(top(), "Reader");
  const std::string opened = app_->reader()->get_path();
  tap(Button::Button0);
  ASSERT_EQ(top(), "Home");
  EXPECT_EQ(home().selected(), 1);
  // Reopen the first cover: it must be the book just read.
  tap(Button::Button1);
  ASSERT_EQ(top(), "Reader");
  EXPECT_EQ(app_->reader()->get_path(), opened);
}

TEST_F(HomeScreenTest, CoversAreCachedAsThumbnails) {
  start_app();
  read_from_library("with_cover.epub");  // has a JPEG cover
  read_from_library("basic.epub");       // has none
  const auto files = thumbs();
  ASSERT_EQ(files.size(), 2u);
  int with_image = 0;
  for (const auto& p : files) {
    std::ifstream f(p, std::ios::binary);
    char hdr[8] = {};
    f.read(hdr, 8);
    ASSERT_EQ(std::string(hdr, 4), "SMT1");
    const int w = static_cast<uint8_t>(hdr[4]) | (static_cast<uint8_t>(hdr[5]) << 8);
    const int h = static_cast<uint8_t>(hdr[6]) | (static_cast<uint8_t>(hdr[7]) << 8);
    if (w > 0) {
      ++with_image;
      EXPECT_GT(h, w) << "a portrait cover";
      EXPECT_EQ(fs::file_size(p), 8u + static_cast<size_t>((w + 7) / 8) * h);
    } else {
      EXPECT_EQ(fs::file_size(p), 8u) << "no cover: an empty marker, so it isn't decoded again";
    }
  }
  EXPECT_EQ(with_image, 1);
}

TEST_F(HomeScreenTest, FinishedLinkAndLibraryAndBackToSettings) {
  start_app();
  read_from_library("basic.epub");
  // ▲ from the first cover → Finished Books.
  tap(Button::Button3);
  ASSERT_EQ(home().selected(), 0);
  tap(Button::Button1);
  ASSERT_EQ(top(), "Finished");
  tap(Button::Button0);
  ASSERT_EQ(top(), "Home");
  EXPECT_EQ(home().selected(), 0) << "back on the Finished Books link";

  // Library's ◀ returns to the home page (not Settings).
  go_library();
  tap(Button::Button0);
  EXPECT_EQ(top(), "Home");
  EXPECT_EQ(home().selected(), library_row());

  // ◀ on the home page opens Settings.
  tap(Button::Button0);
  EXPECT_EQ(top(), "Settings");
}

TEST_F(HomeScreenTest, FinishedBooksAreLeftOff) {
  start_app();
  read_from_library("basic.epub");
  read_from_library("with_cover.epub");
  ASSERT_EQ(home().book_count(), 2);
  const std::string first = book_path("with_cover.epub");
  {
    // Same title/author the index holds, so the shelf matches it.
    const auto& e = BookIndex::instance();
    (void)e;
  }
  app_->finished_books().set(first, "", "", true, 0);
  tap(Button::Button0);  // to Settings and back re-reads the list
  tap(Button::Button0);
  ASSERT_EQ(top(), "Home");
  EXPECT_EQ(home().book_count(), 1);
}

TEST_F(HomeScreenTest, SettingSwitchesBetweenCoversAndBookList) {
  start_app();
  tap(Button::Button0);
  ASSERT_EQ(top(), "Settings");
  // 0 Wi-Fi, (gap), 2 Stats, 3 Goal, 4 Date, (gap), 6 Home Screen.
  select_settings_row(6);
  tap(Button::Button1);
  const bool switched = !app_->home_covers();
  ASSERT_TRUE(switched);
  tap(Button::Button0);
  EXPECT_EQ(top(), "Books") << "the book list is the start page now";
  EXPECT_NE(settings_text().find("home=list"), std::string::npos);

  // It sticks after a restart.
  app_.reset();
  covers_home_ = true;  // the saved setting wins over the default
  app_ = std::make_unique<Application>();
  app_->set_books_dir(books_dir_str_.c_str());
  app_->set_data_dir(data_dir_str_.c_str());
  app_->start(buf_, rt_);
  tick();
  // (Settings was open when the setting was saved, so it reopens on top.)
  if (top() == "Settings")
    tap(Button::Button0);
  EXPECT_EQ(top(), "Books");
}

TEST_F(HomeScreenTest, FontSizeAndNewFontsInSettings) {
  start_app();
  const int before = app_->reader()->reader_settings().font_size_idx;
  tap(Button::Button0);
  ASSERT_EQ(top(), "Settings");
  // ... 6 Home Screen, 7 Display, 8 Menu Size, 9 Book List, 10 Sort, 11 Font, 12 Font Size.
  select_settings_row(11);
  std::vector<std::string> fonts{app_->custom_font_path()};
  for (int k = 0; k < 4; ++k) {
    tap(Button::Button1);
    fonts.push_back(app_->custom_font_path());
  }
  EXPECT_NE(std::find(fonts.begin(), fonts.end(), "Literata"), fonts.end());
  EXPECT_NE(std::find(fonts.begin(), fonts.end(), "Atkinson Hyperlegible"), fonts.end());
  EXPECT_EQ(fonts.back(), "Bookerly") << "four built-in fonts, back to the first";

  select_settings_row(12);
  tap(Button::Button1);
  const bool changed = app_->reader()->reader_settings().font_size_idx != before;
  ASSERT_TRUE(changed);
  EXPECT_EQ(app_->reader()->reader_settings().font_size_idx, (before + 1) % 7);
  char expect[24];
  std::snprintf(expect, sizeof(expect), "font_size=%d", (before + 1) % 7);
  EXPECT_NE(settings_text().find(expect), std::string::npos);
}
