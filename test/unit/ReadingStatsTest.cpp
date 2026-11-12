// Unit tests for lib/microreader/stats/ReadingStats — calendar math, the
// reading-time recorder, the month file format and streaks.

#include "microreader/stats/ReadingStats.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "microreader/Runtime.h"

namespace fs = std::filesystem;
using namespace microreader;

namespace {

// Controllable clocks.
class FakeClock final : public IRuntime {
 public:
  bool should_continue() const override {
    return true;
  }
  uint32_t frame_time_ms() const override {
    return 50;
  }
  void wait_next_frame() override {}
  std::optional<uint8_t> battery_percentage() const override {
    return std::nullopt;
  }
  uint32_t monotonic_ms() const override {
    return mono;
  }
  int64_t local_time() const override {
    return wall;
  }
  bool set_local_time(int64_t t) override {
    wall = t;
    return true;
  }

  // Advance both clocks together.
  void advance_ms(uint32_t ms) {
    mono += ms;
    wall_ms_ += ms;
    wall += wall_ms_ / 1000;
    wall_ms_ %= 1000;
  }

  uint32_t mono = 5000;
  int64_t wall = 0;

 private:
  uint32_t wall_ms_ = 0;
};

int64_t at(int y, int m, int d, int hh = 12, int mm = 0) {
  return days_from_civil(y, m, d) * 86400 + hh * 3600 + mm * 60;
}

class StatsDirTest : public ::testing::Test {
 protected:
  fs::path data_dir_;
  std::string stats_dir_;

  void SetUp() override {
    data_dir_ = fs::temp_directory_path() /
                ("reading_stats_test_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    fs::remove_all(data_dir_);
    fs::create_directories(data_dir_);
    stats_dir_ = (data_dir_ / "stats").string();
  }
  void TearDown() override {
    fs::remove_all(data_dir_);
  }

  void write_month(int y, int m, const std::string& content) {
    fs::create_directories(stats_dir_);
    std::ofstream(ReadingStats::month_path(stats_dir_, y, m), std::ios::binary) << content;
  }
};

}  // namespace

// ── Calendar math ───────────────────────────────────────────────────────────

TEST(CalendarMath, KnownDates) {
  EXPECT_EQ(days_from_civil(1970, 1, 1), 0);
  EXPECT_EQ(days_from_civil(1969, 12, 31), -1);
  EXPECT_EQ(days_from_civil(2000, 3, 1), 11017);
  EXPECT_EQ(days_from_civil(2024, 1, 1) * 86400, kClockValidAfter);
}

TEST(CalendarMath, RoundTrip) {
  for (int64_t d = -800000; d <= 800000; d += 37) {
    const CivilDate c = civil_from_days(d);
    ASSERT_EQ(days_from_civil(c), d) << c.year << "-" << c.month << "-" << c.day;
    ASSERT_GE(c.month, 1);
    ASSERT_LE(c.month, 12);
    ASSERT_GE(c.day, 1);
    ASSERT_LE(c.day, days_in_month(c.year, c.month));
  }
}

TEST(CalendarMath, DaysInMonthAndLeapYears) {
  EXPECT_EQ(days_in_month(2024, 2), 29);
  EXPECT_EQ(days_in_month(2026, 2), 28);
  EXPECT_EQ(days_in_month(2000, 2), 29);
  EXPECT_EQ(days_in_month(2100, 2), 28);
  EXPECT_EQ(days_in_month(2026, 4), 30);
  EXPECT_EQ(days_in_month(2026, 12), 31);
}

TEST(CalendarMath, Weekdays) {
  EXPECT_EQ(weekday_monday0({1970, 1, 1}), 3);   // Thursday
  EXPECT_EQ(weekday_monday0({2026, 11, 9}), 0);  // Monday
  EXPECT_EQ(weekday_monday0({2026, 11, 1}), 6);  // Sunday
  EXPECT_EQ(weekday_monday0({2024, 2, 29}), 3);  // Thursday
  EXPECT_STREQ(weekday_name(0), "Monday");
  EXPECT_STREQ(month_abbrev(11), "Nov");
}

TEST(CalendarMath, LocalTimeSplit) {
  const int64_t t = at(2026, 11, 9, 23, 59) + 59;
  EXPECT_EQ(date_from_local_time(t), (CivilDate{2026, 11, 9}));
  EXPECT_EQ(seconds_of_day(t), 86399);
  EXPECT_EQ(date_from_local_time(t + 1), (CivilDate{2026, 11, 10}));
  EXPECT_EQ(date_from_local_time(-1), (CivilDate{1969, 12, 31}));
  EXPECT_EQ(seconds_of_day(-1), 86399);
}

TEST(CalendarMath, FirmwareBuildTimeIsPlausible) {
  EXPECT_TRUE(clock_is_valid(firmware_build_time()));
  EXPECT_FALSE(clock_is_valid(0));
}

TEST(FormatDuration, Variants) {
  EXPECT_EQ(format_duration(0), "0m");
  EXPECT_EQ(format_duration(20), "<1m");
  EXPECT_EQ(format_duration(30), "1m");
  EXPECT_EQ(format_duration(45 * 60), "45m");
  EXPECT_EQ(format_duration(3600), "1h");
  EXPECT_EQ(format_duration(3900), "1h 5m");
  EXPECT_EQ(format_duration(3900, true), "1h05");
  EXPECT_EQ(format_duration(10 * 3600 + 30 * 60, true), "10h30");
}

TEST(SanitizeTitle, ControlCharsTrimAndUtf8Truncation) {
  EXPECT_EQ(ReadingStats::sanitize_title("  The\tHobbit\n "), "The Hobbit");
  // 79 ASCII bytes + a 2-byte character straddling the 80-byte limit.
  std::string t(79, 'a');
  t += "\xc3\xa9tail";
  const std::string s = ReadingStats::sanitize_title(t);
  EXPECT_EQ(s.size(), 79u);  // cut before the multi-byte character, never inside it
}

// ── File format ─────────────────────────────────────────────────────────────

TEST_F(StatsDirTest, AppendAndLoadAggregates) {
  ASSERT_TRUE(ReadingStats::append_record(stats_dir_, {2026, 11, 9}, 600, 10, "Book A"));
  ASSERT_TRUE(ReadingStats::append_record(stats_dir_, {2026, 11, 9}, 300, 5, "Book B"));
  ASSERT_TRUE(ReadingStats::append_record(stats_dir_, {2026, 11, 9}, 60, 1, "Book A"));
  ASSERT_TRUE(ReadingStats::append_record(stats_dir_, {2026, 11, 10}, 120, 2, "Book A"));
  ASSERT_TRUE(ReadingStats::append_record(stats_dir_, {2026, 12, 1}, 999, 9, "Book C"));  // other month

  MonthStats m;
  ASSERT_TRUE(m.load(stats_dir_, 2026, 11));
  EXPECT_EQ(m.day(9).seconds, 960u);
  EXPECT_EQ(m.day(9).pages, 16u);
  EXPECT_EQ(m.day(10).seconds, 120u);
  EXPECT_EQ(m.total_seconds(), 1080u);
  EXPECT_EQ(m.total_pages(), 18u);
  EXPECT_EQ(m.days_read(), 2);
  EXPECT_EQ(m.longest_streak(), 2);
  ASSERT_EQ(m.titles.size(), 2u);
  // (day 9, A), (day 9, B), (day 10, A)
  ASSERT_EQ(m.books.size(), 3u);
  for (const auto& b : m.books)
    if (b.day == 9 && m.titles[b.title] == "Book A")
      EXPECT_EQ(b.seconds, 660u);
}

TEST_F(StatsDirTest, NothingRecordedForEmptyChunk) {
  EXPECT_FALSE(ReadingStats::append_record(stats_dir_, {2026, 11, 9}, 0, 0, "Book"));
  EXPECT_FALSE(fs::exists(ReadingStats::month_path(stats_dir_, 2026, 11)));
}

TEST_F(StatsDirTest, MissingMonthIsEmpty) {
  MonthStats m;
  EXPECT_FALSE(m.load(stats_dir_, 2025, 1));
  EXPECT_EQ(m.total_seconds(), 0u);
  EXPECT_EQ(m.year, 2025);
  EXPECT_EQ(m.month, 1);
}

TEST_F(StatsDirTest, LoadSkipsMalformedLines) {
  write_month(2026, 2,
              "# header comment\r\n"
              "3 100 2 Good Book\r\n"
              "garbage line\n"
              "30 500 1 Feb 30 does not exist\n"
              "0 500 1 day zero\n"
              "4 notanumber 1 x\n"
              "5 999999 1 Capped To A Day\n"
              "\n"
              "6 50 0\n"           // no title
              "7 70 1 Partial last li");  // no trailing newline (power cut mid-write)
  MonthStats m;
  ASSERT_TRUE(m.load(stats_dir_, 2026, 2));
  EXPECT_EQ(m.day(3).seconds, 100u);
  EXPECT_EQ(m.day(3).pages, 2u);
  EXPECT_EQ(m.day(4).seconds, 0u);
  EXPECT_EQ(m.day(5).seconds, 86400u);
  EXPECT_EQ(m.day(6).seconds, 50u);
  EXPECT_EQ(m.day(7).seconds, 70u);
  EXPECT_EQ(m.days_read(), 4);
  bool has_unknown = false;
  for (const auto& t : m.titles)
    has_unknown |= (t == "Unknown book");
  EXPECT_TRUE(has_unknown);
}

TEST_F(StatsDirTest, OverLongLineIsSkipped) {
  std::string big(500, 'x');
  write_month(2026, 3, "1 10 1 " + big + "\n2 20 2 Fine\n");
  MonthStats m;
  m.load(stats_dir_, 2026, 3);
  EXPECT_EQ(m.day(1).seconds, 0u);
  EXPECT_EQ(m.day(2).seconds, 20u);
}

TEST_F(StatsDirTest, TitleOverflowFoldsIntoOther) {
  std::string content;
  for (size_t i = 0; i < MonthStats::kMaxTitles + 5; ++i)
    content += "1 10 1 Title " + std::to_string(i) + "\n";
  write_month(2026, 4, content);
  MonthStats m;
  m.load(stats_dir_, 2026, 4);
  EXPECT_EQ(m.titles.size(), MonthStats::kMaxTitles + 1);
  EXPECT_EQ(m.titles.back(), "Other books");
  EXPECT_EQ(m.day(1).seconds, 10u * (MonthStats::kMaxTitles + 5));
}

// ── Recorder ────────────────────────────────────────────────────────────────

class RecorderTest : public StatsDirTest {
 protected:
  FakeClock clock_;
  ReadingStats stats_;

  void SetUp() override {
    StatsDirTest::SetUp();
    stats_.set_data_dir(data_dir_.string().c_str());
    stats_.set_clock(&clock_);
    clock_.wall = at(2026, 11, 9, 20, 0);
  }

  MonthStats month(int y = 2026, int m = 11) {
    MonthStats ms;
    ms.load(stats_dir_, y, m);
    return ms;
  }
};

TEST_F(RecorderTest, CountsTimeAndPagesRead) {
  stats_.book_opened("The Hobbit");
  clock_.advance_ms(30000);
  stats_.page_turned(true);  // 30 s on page 1 → counts
  clock_.advance_ms(1000);
  stats_.page_turned(true);  // 1 s: skimmed, time counts but not the page
  clock_.advance_ms(40000);
  stats_.page_turned(false);  // backwards: time counts, no page
  clock_.advance_ms(20000);
  stats_.book_closed();  // current page time counts, no page turn

  const MonthStats m = month();
  EXPECT_EQ(m.day(9).seconds, 91u);
  EXPECT_EQ(m.day(9).pages, 1u);
  ASSERT_EQ(m.titles.size(), 1u);
  EXPECT_EQ(m.titles[0], "The Hobbit");
}

TEST_F(RecorderTest, IdlePageIsCapped) {
  stats_.book_opened("Book");
  clock_.advance_ms(2 * 3600 * 1000);  // left the device on a page for 2 h
  stats_.page_turned(true);
  stats_.book_closed();
  EXPECT_EQ(month().day(9).seconds, ReadingStats::kMaxPageMs / 1000);
  EXPECT_EQ(month().day(9).pages, 1u);
}

TEST_F(RecorderTest, PausedTimeIsNotCounted) {
  stats_.book_opened("Book");
  clock_.advance_ms(10000);
  stats_.reading_paused();  // reader options opened
  EXPECT_FALSE(stats_.is_timing());
  clock_.advance_ms(60000);
  stats_.reading_resumed();
  clock_.advance_ms(5000);
  stats_.book_closed();
  EXPECT_EQ(month().day(9).seconds, 15u);
}

TEST_F(RecorderTest, SubSecondRemaindersCarryOver) {
  stats_.book_opened("Book");
  for (int i = 0; i < 4; ++i) {
    clock_.advance_ms(2500);
    stats_.page_turned(true);
  }
  stats_.book_closed();
  EXPECT_EQ(month().day(9).seconds, 10u);
  EXPECT_EQ(month().day(9).pages, 4u);
}

TEST_F(RecorderTest, MidnightSplitsAcrossDaysAndMonths) {
  clock_.wall = at(2026, 11, 30, 23, 59);
  stats_.book_opened("Book");
  clock_.advance_ms(30000);
  stats_.page_turned(true);  // still 30 Nov
  clock_.advance_ms(40000);  // now 1 Dec 00:00:10
  stats_.page_turned(true);
  stats_.book_closed();
  EXPECT_EQ(month(2026, 11).day(30).seconds, 30u);
  EXPECT_EQ(month(2026, 11).day(30).pages, 1u);
  EXPECT_EQ(month(2026, 12).day(1).seconds, 40u);
  EXPECT_EQ(month(2026, 12).day(1).pages, 1u);
}

TEST_F(RecorderTest, PeriodicFlushBoundsDataLoss) {
  stats_.book_opened("Book");
  for (int i = 0; i < 6; ++i) {  // 6 × 2 min = 12 min > kFlushEveryMs
    clock_.advance_ms(120000);
    stats_.page_turned(true);
  }
  // Without closing the book, at least the first 10 minutes are on disk.
  EXPECT_GE(month().day(9).seconds, ReadingStats::kFlushEveryMs / 1000);
  EXPECT_LT(stats_.pending_ms(), ReadingStats::kFlushEveryMs);
}

TEST_F(RecorderTest, NoWallClockRecordsNothing) {
  clock_.wall = 0;  // never set
  stats_.book_opened("Book");
  clock_.advance_ms(60000);
  stats_.page_turned(true);
  stats_.book_closed();
  EXPECT_FALSE(fs::exists(stats_dir_));
}

TEST_F(RecorderTest, NoClockAtAllIsHarmless) {
  ReadingStats s;
  s.set_data_dir(data_dir_.string().c_str());
  s.book_opened("Book");
  s.page_turned(true);
  s.reading_paused();
  s.reading_resumed();
  s.book_closed();
  s.flush();
  EXPECT_FALSE(fs::exists(stats_dir_));
}

TEST_F(RecorderTest, OpeningAnotherBookClosesThePrevious) {
  stats_.book_opened("First");
  clock_.advance_ms(10000);
  stats_.book_opened("Second");
  clock_.advance_ms(20000);
  stats_.book_closed();
  const MonthStats m = month();
  ASSERT_EQ(m.titles.size(), 2u);
  for (const auto& b : m.books)
    EXPECT_EQ(b.seconds, m.titles[b.title] == "First" ? 10u : 20u);
}

TEST_F(RecorderTest, StreakSpansMonthsAndToleratesUnreadToday) {
  ReadingStats::append_record(stats_dir_, {2026, 10, 30}, 60, 1, "B");
  ReadingStats::append_record(stats_dir_, {2026, 10, 31}, 60, 1, "B");
  ReadingStats::append_record(stats_dir_, {2026, 11, 1}, 60, 1, "B");
  ReadingStats::append_record(stats_dir_, {2026, 11, 2}, 60, 1, "B");
  EXPECT_EQ(stats_.current_streak({2026, 11, 2}), 4);
  EXPECT_EQ(stats_.current_streak({2026, 11, 3}), 4);  // nothing yet today
  EXPECT_EQ(stats_.current_streak({2026, 11, 4}), 0);  // missed a day
  EXPECT_EQ(stats_.current_streak({2026, 10, 30}), 1);
}

// ── Daily goal ──────────────────────────────────────────────────────────────

TEST(DailyGoal, DayCounts) {
  DayTotals t;
  EXPECT_FALSE(t.counts(0));
  t.pages = 1;
  EXPECT_TRUE(t.counts(0));  // no goal: any reading counts
  EXPECT_FALSE(t.counts(60));
  t.seconds = 1799;
  EXPECT_FALSE(t.counts(1800));
  t.seconds = 1800;
  EXPECT_TRUE(t.counts(1800));  // reaching the goal exactly counts
}

TEST_F(StatsDirTest, GoalChangesDaysCountedAndLongestStreak) {
  // 1..4 Mar: 40m, 10m, 35m, 31m; 6 Mar: 5m.
  write_month(2026, 3, "1 2400 10 A\n2 600 3 A\n3 2100 9 A\n4 1860 8 A\n6 300 1 A\n");
  MonthStats m;
  m.load(stats_dir_, 2026, 3);
  EXPECT_EQ(m.days_read(), 5);
  EXPECT_EQ(m.days_counted(0), 5);
  EXPECT_EQ(m.longest_streak(0), 4);
  EXPECT_EQ(m.days_counted(30 * 60), 3);
  EXPECT_EQ(m.longest_streak(30 * 60), 2);  // 3 and 4 Mar
  EXPECT_EQ(m.longest_streak(60 * 60), 0);
}

TEST_F(RecorderTest, CurrentStreakWithGoal) {
  ReadingStats::append_record(stats_dir_, {2026, 11, 6}, 1800, 1, "B");
  ReadingStats::append_record(stats_dir_, {2026, 11, 7}, 600, 1, "B");  // short day
  ReadingStats::append_record(stats_dir_, {2026, 11, 8}, 2000, 1, "B");
  ReadingStats::append_record(stats_dir_, {2026, 11, 9}, 300, 1, "B");  // today, goal not reached yet
  EXPECT_EQ(stats_.current_streak({2026, 11, 9}, 0), 4);
  // With a 30-minute goal the short day breaks it; today doesn't count yet but doesn't break it.
  EXPECT_EQ(stats_.current_streak({2026, 11, 9}, 30 * 60), 1);
  ReadingStats::append_record(stats_dir_, {2026, 11, 9}, 1600, 1, "B");  // now 31m today
  EXPECT_EQ(stats_.current_streak({2026, 11, 9}, 30 * 60), 2);
}

TEST(DailyGoal, PresetsIncludeDefault) {
  bool found = false;
  for (int i = 0; i < kNumGoalPresets; ++i)
    found |= kGoalPresetMinutes[i] == kDefaultGoalMinutes;
  EXPECT_TRUE(found);
}
