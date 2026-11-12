// Integration tests for the reading-statistics feature, driven through the
// real Application: clock restore/persistence, ReaderScreen recording,
// StatsCalendarScreen navigation and DateTimeScreen.
//
// Set STATS_SCREENSHOT_DIR to dump every refreshed frame as a PBM (raw
// 800x480 physical panel bits) for visual inspection.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "microreader/Application.h"
#include "microreader/Runtime.h"
#include "microreader/display/DrawBuffer.h"
#include "microreader/stats/ReadingStats.h"

namespace fs = std::filesystem;
using namespace microreader;

namespace {

class FakeRuntime final : public IRuntime {
 public:
  bool should_continue() const override {
    return true;
  }
  uint32_t frame_time_ms() const override {
    return 50;
  }
  void wait_next_frame() override {}
  std::optional<uint8_t> battery_percentage() const override {
    return 80;
  }
  uint32_t monotonic_ms() const override {
    return mono;
  }
  int64_t local_time() const override {
    return wall;
  }
  bool set_local_time(int64_t t) override {
    if (!settable)
      return false;
    wall = t;
    return true;
  }
  void advance_ms(uint32_t ms) {
    mono += ms;
    wall += ms / 1000;
  }

  uint32_t mono = 1000;
  int64_t wall = 0;
  bool settable = true;
};

// Records the last frame pushed to the panel.
class CaptureDisplay final : public IDisplay {
 public:
  void full_refresh(const uint8_t* pixels, RefreshMode, bool) override {
    capture(pixels);
  }
  void partial_refresh(const uint8_t* new_pixels, const uint8_t*) override {
    capture(new_pixels);
  }
  void capture(const uint8_t* p) {
    last.assign(p, p + DisplayFrame::kPixelBytes);
    ++frames;
    if (const char* dir = std::getenv("STATS_SCREENSHOT_DIR")) {
      char name[64];
      std::snprintf(name, sizeof(name), "/%s_%03d.pbm", tag.c_str(), frames);
      std::ofstream f(std::string(dir) + name, std::ios::binary);
      f << "P4\n" << DisplayFrame::kPanelWidth << " " << DisplayFrame::kPhysicalHeight << "\n";
      for (uint8_t b : last)
        f.put(static_cast<char>(~b));  // PBM: 1 = black; panel: 1 = white
    }
  }
  int black_pixels() const {
    int n = 0;
    for (uint8_t b : last)
      for (int i = 0; i < 8; ++i)
        n += ((b >> i) & 1) ? 0 : 1;
    return n;
  }

  std::vector<uint8_t> last;
  int frames = 0;
  std::string tag = "frame";
};

ButtonState press(Button b) {
  ButtonState s;
  s.pressed_latch = static_cast<uint8_t>(1u << static_cast<uint8_t>(b));
  s.press_history[0] = static_cast<uint8_t>(b);
  s.press_history_count = 1;
  return s;
}

int64_t at(int y, int m, int d, int hh = 12, int mm = 0) {
  return days_from_civil(y, m, d) * 86400 + hh * 3600 + mm * 60;
}

class StatsScreensTest : public ::testing::Test {
 protected:
  fs::path data_dir_;
  std::string data_dir_str_;  // Application keeps a raw pointer to this
  CaptureDisplay display_;
  DrawBuffer buf_{display_};
  FakeRuntime rt_;
  std::unique_ptr<Application> app_;

  void SetUp() override {
    data_dir_ = fs::temp_directory_path() /
                ("stats_screens_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    fs::remove_all(data_dir_);
    fs::create_directories(data_dir_ / "cache");
    fs::create_directories(data_dir_ / "data");
    data_dir_str_ = data_dir_.string();
    display_.tag = ::testing::UnitTest::GetInstance()->current_test_info()->name();
  }
  void TearDown() override {
    app_.reset();
    fs::remove_all(data_dir_);
  }

  void start_app() {
    app_ = std::make_unique<Application>();
    app_->set_data_dir(data_dir_str_.c_str());
    app_->set_home_covers(false);  // these tests start from the book list
    app_->start(buf_, rt_);
    tick();  // let the main menu settle
  }
  void tick(const ButtonState& b = ButtonState{}) {
    app_->update(b, 50, buf_, rt_);
  }
  void tap(Button b) {
    tick(press(b));
  }
  std::string top() const {
    return app_->top_screen_name();
  }
  void open_stats() {
    app_->push_screen(ScreenId::Stats);
    tick();
    ASSERT_EQ(top(), "Stats");
  }
  const StatsCalendarScreen& cal() const {
    return *app_->stats_screen();
  }
  const DateTimeScreen& clock_screen() const {
    return *app_->date_time_screen();
  }
};

}  // namespace

// ── Clock restore / persistence ─────────────────────────────────────────────

TEST_F(StatsScreensTest, ColdBootClockStartsAtBuildTimeWhenNeverSaved) {
  rt_.wall = 0;  // ESP32 after a cold power-on
  start_app();
  EXPECT_EQ(rt_.wall, firmware_build_time());
}

TEST_F(StatsScreensTest, ColdBootClockRestoresSavedTime) {
  const int64_t later = firmware_build_time() + 40 * 86400;
  rt_.wall = later;
  start_app();
  app_->save_settings_();  // writes clock=
  app_.reset();

  rt_.wall = 0;  // power lost
  start_app();
  EXPECT_EQ(rt_.wall, later);
}

TEST_F(StatsScreensTest, ValidClockIsLeftAlone) {
  rt_.wall = at(2030, 5, 5);
  start_app();
  EXPECT_EQ(rt_.wall, at(2030, 5, 5));
}

// ── ReaderScreen → stats file ───────────────────────────────────────────────

TEST_F(StatsScreensTest, ReadingABookIsRecorded) {
  rt_.wall = at(2026, 11, 9, 21, 0);
  start_app();
  const std::string book = std::string(TEST_FIXTURES_DIR) + "/large_chapter.epub";
  app_->auto_open_book(book.c_str(), buf_, rt_);
  ASSERT_EQ(top(), "Reader");
  ASSERT_TRUE(app_->reader()->is_open_ok());

  for (int i = 0; i < 3; ++i) {
    rt_.advance_ms(20000);
    tap(Button::Up);  // side Up = next page (default)
  }
  // Reader options cover the reader: that time must not count.
  tap(Button::Button1);
  ASSERT_EQ(top(), "Options");
  rt_.advance_ms(5 * 60 * 1000);
  tap(Button::Button0);
  ASSERT_EQ(top(), "Reader");
  rt_.advance_ms(10000);
  tap(Button::Button0);  // back to the book list → book closed → flushed
  ASSERT_EQ(top(), "Books");

  MonthStats m;
  ASSERT_TRUE(m.load(app_->stats().stats_dir(), 2026, 11));
  EXPECT_EQ(m.day(9).seconds, 70u);
  EXPECT_EQ(m.day(9).pages, 3u);
  ASSERT_EQ(m.titles.size(), 1u);
  EXPECT_FALSE(m.titles[0].empty());
}

TEST_F(StatsScreensTest, SleepFlushesReadingUnderOptionsMenu) {
  rt_.wall = at(2026, 11, 9, 21, 0);
  start_app();
  const std::string book = std::string(TEST_FIXTURES_DIR) + "/multi_chapter.epub";
  app_->auto_open_book(book.c_str(), buf_, rt_);
  rt_.advance_ms(30000);
  tap(Button::Button1);  // options on top, reader paused underneath
  tap(Button::Power);    // sleep
  EXPECT_FALSE(app_->running());

  MonthStats m;
  m.load(app_->stats().stats_dir(), 2026, 11);
  EXPECT_EQ(m.day(9).seconds, 30u);
}

// ── Calendar screen ─────────────────────────────────────────────────────────

TEST_F(StatsScreensTest, CalendarOpensOnTodayAndNavigates) {
  rt_.wall = at(2026, 11, 9, 10, 31);
  start_app();
  const std::string dir = app_->stats().stats_dir();
  ReadingStats::append_record(dir, {2026, 11, 2}, 1500, 14, "Piranesi");
  ReadingStats::append_record(dir, {2026, 11, 3}, 300, 3, "Piranesi");
  ReadingStats::append_record(dir, {2026, 11, 5}, 5400, 70, "Piranesi");
  ReadingStats::append_record(dir, {2026, 11, 7}, 1800, 20, "The Hobbit");
  ReadingStats::append_record(dir, {2026, 11, 8}, 4200, 55, "The Hobbit");
  ReadingStats::append_record(dir, {2026, 11, 8}, 900, 12, "A Wizard of Earthsea");
  ReadingStats::append_record(dir, {2026, 11, 8}, 400, 4, "The Left Hand of Darkness: a very long title indeed");
  ReadingStats::append_record(dir, {2026, 11, 8}, 200, 2, "Dune");
  ReadingStats::append_record(dir, {2026, 11, 9}, 2700, 30, "A Wizard of Earthsea");
  ReadingStats::append_record(dir, {2026, 10, 31}, 600, 8, "Dune");

  open_stats();
  EXPECT_EQ(cal().view_year(), 2026);
  EXPECT_EQ(cal().view_month(), 11);
  EXPECT_EQ(cal().selected_day(), 9);
  EXPECT_EQ(cal().streak(), 3);  // 7, 8, 9 Nov
  EXPECT_EQ(cal().month().total_seconds(), 1500u + 300 + 5400 + 1800 + 4200 + 900 + 400 + 200 + 2700);
  EXPECT_GT(display_.black_pixels(), 2000);

  // ▲ (Button3 with default Menu Nav) = previous day.
  tap(Button::Button3);
  EXPECT_EQ(cal().selected_day(), 8);
  // ▼ twice = next day ×2.
  tap(Button::Button2);
  tap(Button::Button2);
  EXPECT_EQ(cal().selected_day(), 10);

  // Day steps cross month boundaries.
  for (int i = 0; i < 10; ++i)
    tap(Button::Button3);
  EXPECT_EQ(cal().view_month(), 10);
  EXPECT_EQ(cal().selected_day(), 31);
  EXPECT_EQ(cal().month().day(31).seconds, 600u);

  // Side buttons step whole months, clamping the day.
  tap(Button::Down);
  EXPECT_EQ(cal().view_month(), 11);
  EXPECT_EQ(cal().selected_day(), 30);
  tap(Button::Up);
  tap(Button::Up);
  tap(Button::Up);
  EXPECT_EQ(cal().view_month(), 8);
  tap(Button::Down);
  tap(Button::Down);
  tap(Button::Down);
  tap(Button::Down);
  tap(Button::Down);
  EXPECT_EQ(cal().view_year(), 2027);
  EXPECT_EQ(cal().view_month(), 1);

  tap(Button::Button0);
  EXPECT_EQ(top(), "Books");
}

TEST_F(StatsScreensTest, CalendarRespectsMenuNavInversion) {
  rt_.wall = at(2026, 11, 9);
  start_app();
  app_->set_invert_menu_buttons(true);
  open_stats();
  tap(Button::Button2);  // inverted: Button2 is ▲ = previous day
  EXPECT_EQ(cal().selected_day(), 8);
}

TEST_F(StatsScreensTest, CalendarWithoutClockDoesNotCrash) {
  rt_.settable = false;  // no RTC at all: clock stays at 0
  rt_.wall = 0;
  start_app();
  open_stats();
  EXPECT_EQ(cal().streak(), 0);
  tap(Button::Button2);
  tap(Button::Down);
  EXPECT_EQ(top(), "Stats");
}

TEST_F(StatsScreensTest, CalendarLandscapeRenders) {
  rt_.wall = at(2026, 11, 9, 10, 31);
  start_app();
  ReadingStats::append_record(app_->stats().stats_dir(), {2026, 11, 9}, 2700, 30, "Earthsea");
  app_->set_rotate_display(true);
  buf_.set_rotation(Rotation::Deg0);
  open_stats();
  EXPECT_GT(display_.black_pixels(), 2000);
  tap(Button::Button3);
  EXPECT_EQ(cal().selected_day(), 8);
}

// ── Date & time screen ──────────────────────────────────────────────────────

TEST_F(StatsScreensTest, SetDateFromCalendar) {
  rt_.wall = at(2026, 11, 9, 10, 31);
  start_app();
  open_stats();
  tap(Button::Button1);
  ASSERT_EQ(top(), "Date & Time");
  EXPECT_EQ(clock_screen().value(DateTimeScreen::kDay), 9);
  EXPECT_EQ(clock_screen().value(DateTimeScreen::kHour), 10);
  EXPECT_EQ(clock_screen().value(DateTimeScreen::kMinute), 31);

  tap(Button::Button3);  // ▲ day: 10
  tap(Button::Button3);  // 11
  tap(Button::Button1);  // → month
  tap(Button::Button2);  // ▼ month: Oct
  tap(Button::Button1);  // → year
  tap(Button::Button1);  // → hour
  tap(Button::Button2);  // 09
  tap(Button::Button1);  // → minute
  EXPECT_EQ(clock_screen().field(), DateTimeScreen::kMinute);
  tap(Button::Button1);  // save
  EXPECT_EQ(top(), "Stats");
  EXPECT_EQ(rt_.wall, at(2026, 10, 11, 9, 31));
  // The calendar re-opened on the new "today".
  EXPECT_EQ(cal().view_month(), 10);
  EXPECT_EQ(cal().selected_day(), 11);

  // The new time was persisted for cold boots.
  std::ifstream settings(data_dir_ / "settings");
  std::string text((std::istreambuf_iterator<char>(settings)), std::istreambuf_iterator<char>());
  EXPECT_NE(text.find("clock=" + std::to_string(at(2026, 10, 11, 9, 31))), std::string::npos);
}

TEST_F(StatsScreensTest, DateScreenWrapsAndClampsDay) {
  rt_.wall = at(2024, 1, 31);
  start_app();
  app_->push_screen(ScreenId::DateTime);
  tick();
  tap(Button::Button1);  // → month
  tap(Button::Button3);  // Feb: day 31 clamps to 29 (2024 is a leap year)
  EXPECT_EQ(clock_screen().value(DateTimeScreen::kMonth), 2);
  EXPECT_EQ(clock_screen().value(DateTimeScreen::kDay), 29);
  tap(Button::Button1);  // → year
  tap(Button::Button3);  // 2025: no Feb 29 → 28
  EXPECT_EQ(clock_screen().value(DateTimeScreen::kDay), 28);
  tap(Button::Button1);  // → hour
  tap(Button::Button2);  // 12 → 11
  tap(Button::Button1);  // → minute
  tap(Button::Button2);  // 00 → 59 (wraps)
  EXPECT_EQ(clock_screen().value(DateTimeScreen::kMinute), 59);
}

TEST_F(StatsScreensTest, DateScreenBackCancelsWithoutChange) {
  rt_.wall = at(2026, 11, 9, 10, 31);
  start_app();
  app_->push_screen(ScreenId::DateTime);
  tick();
  tap(Button::Button3);  // change day
  tap(Button::Button1);  // → month
  tap(Button::Button0);  // ← day
  EXPECT_EQ(clock_screen().field(), DateTimeScreen::kDay);
  tap(Button::Button0);  // cancel
  EXPECT_EQ(top(), "Books");
  EXPECT_EQ(rt_.wall, at(2026, 11, 9, 10, 31));
}

TEST_F(StatsScreensTest, SettingsHasStatsEntries) {
  rt_.wall = at(2026, 11, 9, 10, 31);
  start_app();
  tap(Button::Button0);  // book list ◀ → Settings
  ASSERT_EQ(top(), "Settings");
  EXPECT_EQ(app_->settings()->selected_index(), 0);  // first item: Wi-Fi Transfer
  tap(Button::Button2);  // ▼ Reading Stats (skips the gap)
  tap(Button::Button1);
  EXPECT_EQ(top(), "Stats");
  tap(Button::Button0);
  ASSERT_EQ(top(), "Settings");
  tap(Button::Button2);  // ▼ Daily Goal
  tap(Button::Button2);  // ▼ "Date: ..."
  tap(Button::Button1);
  EXPECT_EQ(top(), "Date & Time");
}
