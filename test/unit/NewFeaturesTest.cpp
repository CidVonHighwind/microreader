// Application-level tests for: daily reading goal, Finished shelf, chapter
// progress bar and the sleep screen modes. Driven through the real
// Application with button presses, like a user would.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "AppHarness.h"
#include "microreader/screens/SleepScreens.h"

namespace fs = std::filesystem;
using namespace microreader;
using namespace app_harness;

namespace {

class NewFeaturesTest : public AppFixture {
 protected:
  void open_settings() {
    tap(Button::Button0);  // book list ◀ → Settings
    ASSERT_EQ(top(), "Settings");
  }
  // ▼ n times (front Button2 with the default Menu Nav).
  void down(int n) {
    for (int i = 0; i < n; ++i)
      tap(Button::Button2);
  }
};

// Runs a test body with the working directory set to `dir` (the desktop build
// reads sleep images from ./sd/.sleep and ./sd/sleep).
class ScopedCwd {
 public:
  explicit ScopedCwd(const fs::path& dir) : old_(fs::current_path()) {
    fs::current_path(dir);
  }
  ~ScopedCwd() {
    fs::current_path(old_);
  }

 private:
  fs::path old_;
};

}  // namespace

// Settings item order: 0 Reading Stats, 1 Daily Goal, [2 Goal target], then
// Date & Time. The book list starts with "Finished Books (N)", "Wi-Fi
// Transfer", a gap, then books.

// ── Daily goal ──────────────────────────────────────────────────────────────

TEST_F(NewFeaturesTest, GoalEnableAndTargetInSettings) {
  rt_.wall = at(2026, 11, 10, 9, 0);
  start_app();
  EXPECT_FALSE(app_->goal_enabled());
  open_settings();
  down(1);
  tap(Button::Button1);  // Daily Goal: Off → On
  EXPECT_TRUE(app_->goal_enabled());
  EXPECT_EQ(app_->goal_minutes(), 30);
  EXPECT_EQ(app_->settings()->selected_index(), 1);  // stays on the toggle after the list rebuilds
  down(1);
  tap(Button::Button1);  // Goal: 30m → 45m
  EXPECT_EQ(app_->goal_minutes(), 45);
  EXPECT_EQ(app_->goal_seconds(), 45u * 60u);
  const std::string s = settings_text();
  EXPECT_NE(s.find("goal_on=1"), std::string::npos);
  EXPECT_NE(s.find("goal_min=45"), std::string::npos);

  // Survives a restart (which also reopens Settings on the goal line).
  app_.reset();
  start_app();
  EXPECT_TRUE(app_->goal_enabled());
  EXPECT_EQ(app_->goal_minutes(), 45);
  ASSERT_EQ(top(), "Settings");
  EXPECT_EQ(app_->settings()->selected_index(), 2);

  // Turning it off removes the target line again and makes goal_seconds 0.
  tap(Button::Button3);  // ▲ to Daily Goal
  tap(Button::Button1);
  EXPECT_FALSE(app_->goal_enabled());
  EXPECT_EQ(app_->goal_seconds(), 0u);
}

TEST_F(NewFeaturesTest, GoalDrivesCalendarStreaks) {
  rt_.wall = at(2026, 11, 10, 21, 15);
  start_app();
  const std::string dir = app_->stats().stats_dir();
  ReadingStats::append_record(dir, {2026, 11, 3}, 2400, 30, "The Lighthouse Keeper");
  ReadingStats::append_record(dir, {2026, 11, 4}, 900, 10, "The Lighthouse Keeper");
  ReadingStats::append_record(dir, {2026, 11, 6}, 1900, 22, "The Lighthouse Keeper");
  ReadingStats::append_record(dir, {2026, 11, 7}, 3600, 41, "Small Hours");
  ReadingStats::append_record(dir, {2026, 11, 8}, 1500, 18, "Small Hours");
  ReadingStats::append_record(dir, {2026, 11, 8}, 600, 6, "The Lighthouse Keeper");
  ReadingStats::append_record(dir, {2026, 11, 9}, 2000, 25, "Small Hours");
  ReadingStats::append_record(dir, {2026, 11, 10}, 840, 9, "Small Hours");  // today: 14m so far

  // No goal: 6..10 Nov is a 5-day streak.
  app_->push_screen(ScreenId::Stats);
  tick();
  EXPECT_EQ(app_->stats_screen()->streak(), 5);
  EXPECT_EQ(app_->stats_screen()->goal_seconds(), 0u);
  tap(Button::Button0);

  // 30-minute goal: 6, 7, 8 (25m+10m) and 9 Nov count; today not yet.
  app_->set_goal_enabled(true);
  app_->set_goal_minutes(30);
  app_->push_screen(ScreenId::Stats);
  tick();
  EXPECT_EQ(app_->stats_screen()->goal_seconds(), 1800u);
  EXPECT_EQ(app_->stats_screen()->streak(), 4);
  EXPECT_EQ(app_->stats_screen()->month().days_counted(1800), 5);
  EXPECT_GT(display_.black_pixels(), 3000);
  tap(Button::Button3);  // previous day (9 Nov) for the screenshot
  EXPECT_EQ(app_->stats_screen()->selected_day(), 9);
}

// ── Finished shelf ──────────────────────────────────────────────────────────

TEST_F(NewFeaturesTest, MarkFinishedFromBookMenuAndShelf) {
  rt_.wall = at(2026, 11, 10, 20, 0);
  use_books({"with_cover.epub", "multi_chapter.epub"});
  start_app();
  ASSERT_EQ(top(), "Books");
  ASSERT_EQ(app_->main_menu()->book_count(), 2);
  EXPECT_EQ(app_->main_menu()->get_item_label(0), "Finished Books (0)");

  const std::string book = book_path("with_cover.epub");
  app_->auto_open_book(book.c_str(), buf_, rt_);
  ASSERT_EQ(top(), "Reader");
  ASSERT_TRUE(app_->reader()->is_open_ok());

  tap(Button::Button1);  // book menu
  ASSERT_EQ(top(), "Options");
  down(1);               // Chapters → Finished
  tap(Button::Button1);  // Finished: No → Yes
  EXPECT_TRUE(app_->finished_books().contains(book, "", ""));
  ASSERT_EQ(app_->finished_books().size(), 1u);
  EXPECT_EQ(app_->finished_books().books()[0].title, "The Lighthouse Keeper");
  EXPECT_EQ(app_->finished_books().books()[0].finished_at, at(2026, 11, 10, 20, 0));

  tap(Button::Button0);  // back to the book
  tap(Button::Button0);  // back to the book list
  ASSERT_EQ(top(), "Books");
  ASSERT_EQ(app_->main_menu()->book_count(), 1);  // moved off the main list
  EXPECT_EQ(app_->main_menu()->get_item_label(0), "Finished Books (1)");
  EXPECT_EQ(app_->main_menu()->current_book_path(), book_path("multi_chapter.epub"));

  // The book list's first row opens the shelf; selecting the book opens it.
  tap(Button::Button3);  // ▲ to "Wi-Fi Transfer" (skips the gap)
  tap(Button::Button3);  // ▲ to "Finished Books (1)"
  EXPECT_EQ(app_->main_menu()->selected_index(), 0);
  EXPECT_EQ(app_->main_menu()->current_book_path(), "");
  tap(Button::Button1);
  ASSERT_EQ(top(), "Finished");
  tap(Button::Button1);
  ASSERT_EQ(top(), "Reader");
  EXPECT_TRUE(app_->reader()->is_open_ok());

  // The book menu offers "Finished: Yes" — selecting it unmarks the book.
  tap(Button::Button1);
  ASSERT_EQ(top(), "Options");
  tap(Button::Button1);  // selection is remembered (still on Finished)
  EXPECT_FALSE(app_->finished_books().contains(book, "", ""));
  tap(Button::Button0);
  tap(Button::Button0);
  EXPECT_EQ(top(), "Finished");  // now-empty shelf
  tap(Button::Button0);
  ASSERT_EQ(top(), "Books");
  EXPECT_EQ(app_->main_menu()->book_count(), 2);  // back on the main list, and the index is intact
  EXPECT_EQ(app_->main_menu()->selected_index(), 0);  // cursor returns to the Finished row
  EXPECT_EQ(app_->main_menu()->get_item_label(0), "Finished Books (0)");
}

TEST_F(NewFeaturesTest, FinishedShelfPersistsAndSurvivesRename) {
  rt_.wall = at(2026, 11, 10, 20, 0);
  use_books({"with_cover.epub", "multi_chapter.epub"});
  start_app();
  app_->finished_books().set(book_path("with_cover.epub"), "The Lighthouse Keeper", "Ada Marlow", true,
                             app_->local_time_now());
  app_.reset();

  // Rename the file on the card and rebuild the index: still finished (matched by title + author).
  fs::rename(book_path("with_cover.epub"), book_path("lighthouse.epub"));
  fs::remove(data_dir_ / "book_index.dat");
  start_app();
  ASSERT_EQ(top(), "Books");
  EXPECT_EQ(app_->finished_books().size(), 1u);
  EXPECT_EQ(app_->main_menu()->book_count(), 1);
  EXPECT_EQ(app_->main_menu()->current_book_path(), book_path("multi_chapter.epub"));
}

// ── Chapter bar ─────────────────────────────────────────────────────────────

TEST_F(NewFeaturesTest, ChapterBarAtBottomWithoutOverlappingText) {
  rt_.wall = at(2026, 11, 10, 20, 0);
  use_books({"large_chapter.epub"});
  start_app();
  app_->reader()->reader_settings().chapter_bar = true;
  const std::string book = book_path("large_chapter.epub");
  app_->auto_open_book(book.c_str(), buf_, rt_);
  buf_.refresh();
  ASSERT_TRUE(app_->reader()->is_open_ok());

  const int H = DrawBuffer::kHeight;
  const int y = H - 2 - ReaderSettings::kChapterBarH;  // bar top (portrait)
  const int x0 = app_->reader()->reader_settings().h_padding();
  const int x1 = DrawBuffer::kWidth - x0 - 1;
  // Outline.
  EXPECT_TRUE(display_.black_at(x0, y + 3));
  EXPECT_TRUE(display_.black_at(x1, y + 3));
  EXPECT_TRUE(display_.black_at(240, y));
  EXPECT_TRUE(display_.black_at(240, y + ReaderSettings::kChapterBarH - 1));
  EXPECT_FALSE(display_.black_at(240, y + 3));  // empty at the start of the chapter
  // Nothing in the gap rows between the text area and the bars.
  for (int row = y - 10; row < y - 4; ++row)
    for (int x = 0; x < DrawBuffer::kWidth; x += 3)
      ASSERT_FALSE(display_.black_at(x, row)) << "x=" << x << " row=" << row;

  // Read on a few pages: the bar fills from the left.
  for (int i = 0; i < 4; ++i)
    tap(Button::Up);
  EXPECT_GT(app_->reader()->chapter_progress_pct(), 0);
  EXPECT_TRUE(display_.black_at(x0 + 3, y + 3));

  // Saved and restored with the other reader settings.
  app_->save_settings_();
  EXPECT_NE(settings_text().find("chapter_bar=1"), std::string::npos);
}

TEST_F(NewFeaturesTest, ChapterBarOffLeavesPageUnchanged) {
  rt_.wall = at(2026, 11, 10, 20, 0);
  use_books({"large_chapter.epub"});
  start_app();
  const std::string book = book_path("large_chapter.epub");
  app_->auto_open_book(book.c_str(), buf_, rt_);
  buf_.refresh();
  const auto off_frame = display_.last;
  tap(Button::Button0);

  // Reopening with the setting explicitly off gives the identical frame.
  app_->reader()->reader_settings().chapter_bar = false;
  app_->auto_open_book(book.c_str(), buf_, rt_);
  buf_.refresh();
  EXPECT_EQ(display_.last, off_frame);
}

// ── Sleep screens ───────────────────────────────────────────────────────────

TEST_F(NewFeaturesTest, SettingsSleepScreenHasThreeModes) {
  start_app();
  EXPECT_EQ(app_->sleep_image_path(), "random");  // default
  open_settings();
  // 0 Stats, 1 Goal, 2 Date, (separator), 4 Display, 5 Menu Size,
  // 6 Book List, 7 Sort, 8 Font, 9 Sleep Screen.
  down(8);
  EXPECT_EQ(app_->settings()->selected_index(), 9);
  tap(Button::Button1);
  EXPECT_EQ(app_->sleep_image_path(), "cover");
  tap(Button::Button1);
  EXPECT_EQ(app_->sleep_image_path(), "stats");
  tap(Button::Button1);
  EXPECT_EQ(app_->sleep_image_path(), "random");  // no per-image entries
}

TEST_F(NewFeaturesTest, OldSleepSettingsBecomeRandom) {
  for (const char* old_value : {"embedded:1", "bmp:/sdcard/.sleep/x.bmp", "img:sd/sleep/a.jpg", ""}) {
    std::ofstream(data_dir_ / "settings") << "v=1\nscreen=menu\nsleep_image=" << old_value << "\n";
    start_app();
    EXPECT_EQ(app_->sleep_image_path(), "random") << old_value;
    app_.reset();
  }
  std::ofstream(data_dir_ / "settings") << "v=1\nscreen=menu\nsleep_image=stats\n";
  start_app();
  EXPECT_EQ(app_->sleep_image_path(), "stats");
}

TEST_F(NewFeaturesTest, SleepShowsStatsCalendar) {
  rt_.wall = at(2026, 11, 10, 22, 30);
  start_app();
  ReadingStats::append_record(app_->stats().stats_dir(), {2026, 11, 10}, 2700, 31, "The Lighthouse Keeper");
  ReadingStats::append_record(app_->stats().stats_dir(), {2026, 11, 9}, 1200, 14, "The Lighthouse Keeper");
  app_->set_sleep_image_path("stats");
  const int refreshes = display_.full_refreshes;
  tap(Button::Power);
  EXPECT_FALSE(app_->running());
  EXPECT_EQ(display_.full_refreshes, refreshes + 1);
  EXPECT_GE(display_.deep_sleeps, 1);
  EXPECT_GT(display_.black_pixels(), 4000);
}

TEST_F(NewFeaturesTest, SleepShowsBookCoverWithProgress) {
  rt_.wall = at(2026, 11, 10, 22, 30);
  use_books({"with_cover.epub"});
  start_app();
  const std::string book = book_path("with_cover.epub");
  app_->auto_open_book(book.c_str(), buf_, rt_);
  for (int i = 0; i < 6; ++i)
    tap(Button::Up);
  const int pct = app_->reader()->progress_pct();
  EXPECT_GT(pct, 0);
  tap(Button::Button0);  // close the book
  EXPECT_EQ(app_->last_book_path(), book);
  EXPECT_EQ(app_->last_book_title(), "The Lighthouse Keeper");
  EXPECT_EQ(app_->last_book_pct(), pct);  // not reset to 0 by closing

  app_->set_sleep_image_path("cover");
  tap(Button::Power);
  EXPECT_FALSE(app_->running());
  EXPECT_GE(display_.full_refreshes, 1);
  const auto cache = data_dir_ / "cache" / "with_cover" / "cover-v1.frame";
  ASSERT_TRUE(fs::exists(cache));
  EXPECT_EQ(fs::file_size(cache), DrawBuffer::kBufSize);
  EXPECT_GT(display_.black_pixels(), 20000);  // a real picture, not a blank page
  const auto first = display_.last;

  // Next sleep uses the cache — even with the book file gone.
  fs::remove(book);
  app_.reset();
  start_app();
  EXPECT_EQ(app_->last_book_pct(), pct);  // restored from settings
  tap(Button::Power);
  EXPECT_EQ(display_.last, first);
}

TEST_F(NewFeaturesTest, CoverFallbacks) {
  // EPUB 3 cover (properties="cover-image"), PNG.
  EXPECT_TRUE(sleep_screens::draw_book_cover(buf_, std::string(TEST_FIXTURES_DIR) + "/with_cover_epub3.epub",
                                             "Small Hours", 42, data_dir_str_.c_str()));
  // A book without any cover image.
  EXPECT_FALSE(sleep_screens::draw_book_cover(buf_, std::string(TEST_FIXTURES_DIR) + "/multi_chapter.epub",
                                              "Multi", 10, data_dir_str_.c_str()));

  // Cover mode with no cover falls back to the sleep images instead of failing.
  use_books({"multi_chapter.epub"});
  start_app();
  const std::string book = book_path("multi_chapter.epub");
  app_->auto_open_book(book.c_str(), buf_, rt_);
  tap(Button::Button0);
  app_->set_sleep_image_path("cover");
  tap(Button::Power);
  EXPECT_FALSE(app_->running());
}

TEST_F(NewFeaturesTest, RandomSleepImagesFromBothFoldersIncludingJpegAndPng) {
  ScopedCwd cwd(root_);
  fs::create_directories(root_ / "sd" / "sleep");
  fs::create_directories(root_ / "sd" / ".sleep");
  fs::copy_file(fs::path(TEST_FIXTURES_DIR) / "sleep_landscape.jpg", root_ / "sd" / "sleep" / "sleep_landscape.jpg");
  fs::copy_file(fs::path(TEST_FIXTURES_DIR) / "sleep_portrait.png", root_ / "sd" / ".sleep" / "sleep_portrait.png");
  std::ofstream(root_ / "sd" / "sleep" / "notes.txt") << "not an image";

  const auto listed = sleep_screens::list_sd_images(true);
  ASSERT_EQ(listed.size(), 2u);
  EXPECT_EQ(listed[0], "img:sd/sleep/sleep_landscape.jpg");
  EXPECT_EQ(listed[1], "img:sd/.sleep/sleep_portrait.png");
  EXPECT_TRUE(sleep_screens::list_sd_images(false).empty());  // need a cache dir

  // Random never shows the same image twice in a row (two images → alternate).
  int last_idx = -1;
  std::vector<uint8_t> frames[2];
  for (int round = 0; round < 4; ++round) {
    start_app();
    app_->set_sleep_image_path("random");
    tap(Button::Power);
    ASSERT_FALSE(app_->running());
    const std::string s = settings_text();
    const size_t p = s.find("sleep_image_idx=");
    ASSERT_NE(p, std::string::npos);
    const int idx = std::atoi(s.c_str() + p + 16);
    ASSERT_TRUE(idx == 0 || idx == 1);
    EXPECT_NE(idx, last_idx);
    last_idx = idx;
    EXPECT_GT(display_.black_pixels(), 5000);
    frames[idx] = display_.last;
    app_.reset();
  }
  EXPECT_NE(frames[0], frames[1]);
  EXPECT_TRUE(fs::exists(data_dir_ / "cache" / "sleep" / "sleep_landscape.jpg.frame"));
  EXPECT_TRUE(fs::exists(data_dir_ / "cache" / "sleep" / "sleep_portrait.png.frame"));
}

TEST_F(NewFeaturesTest, PinnedJpegSleepImage) {
  ScopedCwd cwd(root_);
  fs::create_directories(root_ / "sd" / "sleep");
  fs::copy_file(fs::path(TEST_FIXTURES_DIR) / "sleep_landscape.jpg", root_ / "sd" / "sleep" / "sleep_landscape.jpg");
  start_app();
  app_->set_sleep_image_path("img:sd/sleep/sleep_landscape.jpg");
  tap(Button::Power);
  EXPECT_FALSE(app_->running());
  EXPECT_GE(display_.full_refreshes, 1);
  EXPECT_GT(display_.black_pixels(), 5000);
}

// ── Book list ───────────────────────────────────────────────────────────────

TEST_F(NewFeaturesTest, BookListStartsOnFirstBookBelowFinishedRow) {
  use_books({"multi_chapter.epub", "with_cover.epub"});
  start_app();
  ASSERT_EQ(top(), "Books");
  auto* menu = app_->main_menu();
  EXPECT_EQ(menu->count(), 5);  // Finished row, Wi-Fi row, gap, 2 books
  EXPECT_EQ(menu->get_item_label(1), "Wi-Fi Transfer");
  EXPECT_TRUE(menu->is_separator(2));
  EXPECT_FALSE(menu->is_separator(1));
  EXPECT_EQ(menu->selected_index(), 3);
  EXPECT_FALSE(menu->current_book_path().empty());
  // Going up from the first book lands on the Wi-Fi row, not the gap.
  tap(Button::Button3);
  EXPECT_EQ(menu->selected_index(), 1);
  EXPECT_TRUE(menu->current_book_path().empty());
  tap(Button::Button3);
  EXPECT_EQ(menu->selected_index(), 0);
  // And ▼▼ goes back to the first book.
  tap(Button::Button2);
  tap(Button::Button2);
  EXPECT_EQ(menu->selected_index(), 3);
}

TEST_F(NewFeaturesTest, BookListTitleAndPercentRead) {
  rt_.wall = at(2026, 11, 10, 20, 0);
  use_books({"with_cover.epub", "long_title.epub", "multi_chapter.epub"});
  start_app();
  auto* menu = app_->main_menu();

  // Read a little of two books.
  for (const char* name : {"with_cover.epub", "long_title.epub"}) {
    const std::string book = book_path(name);
    app_->auto_open_book(book.c_str(), buf_, rt_);
    ASSERT_TRUE(app_->reader()->is_open_ok()) << name;
    for (int i = 0; i < 5; ++i)
      tap(Button::Up);
    tap(Button::Button0);
  }
  const int pct_cover = app_->book_progress().get(book_path("with_cover.epub"), "", "");
  EXPECT_GT(pct_cover, 0);

  // Settings → Book List: Title (default) → Title & Percent Read.
  EXPECT_EQ(menu->list_format(), BookListFormat::TitleOnly);
  open_settings();
  down(5);  // 0 Stats, 1 Goal, 2 Date, (gap), 4 Display, 5 Menu Size, 6 Book List
  EXPECT_EQ(app_->settings()->selected_index(), 6);
  tap(Button::Button1);
  EXPECT_EQ(menu->list_format(), BookListFormat::TitleAndPercent);
  tap(Button::Button0);
  ASSERT_EQ(top(), "Books");

  auto label_of = [&](const std::string& path) -> std::string {
    for (int i = 0; i < menu->count(); ++i) {
      if (menu->is_separator(i) || i < MainMenu::kTopRows)
        continue;
      // Select row i and read the highlighted path.
      while (menu->selected_index() != i)
        tap(Button::Button2);
      if (menu->current_book_path() == path)
        return std::string(menu->get_item_label(i));
    }
    return "";
  };
  const std::string cover_label = label_of(book_path("with_cover.epub"));
  EXPECT_EQ(cover_label, "The Lighthouse Keeper  \xc2\xb7  " + std::to_string(pct_cover) + "%");
  EXPECT_EQ(label_of(book_path("multi_chapter.epub")).find('%'), std::string::npos);  // never opened
  const std::string long_label = label_of(book_path("long_title.epub"));
  EXPECT_NE(long_label.find("...  \xc2\xb7  "), std::string::npos) << long_label;  // title shortened
  EXPECT_EQ(long_label.back(), '%');                                               // percent kept

  // Percentages are saved and survive a restart.
  app_.reset();
  start_app();
  EXPECT_EQ(app_->book_progress().get(book_path("with_cover.epub"), "", ""), pct_cover);
  EXPECT_EQ(app_->main_menu()->list_format(), BookListFormat::TitleAndPercent);
  // Matched by title + author too, e.g. after the file was renamed.
  EXPECT_EQ(app_->book_progress().get("/elsewhere/renamed.epub", "The Lighthouse Keeper", "Ada Marlow"), pct_cover);
}
