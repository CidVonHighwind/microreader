#pragma once

// Reading statistics: per-day reading time and pages, stored on the SD card
// and shown by StatsCalendarScreen.
//
// Storage: one append-only text file per month, <data_dir>/stats/YYYY-MM.txt.
// Each line is one flushed chunk of reading:
//
//     <day> <seconds> <pages> <book title>
//
// Lines are summed when a month is loaded, so a power cut can at worst lose
// the last unflushed chunk (and leave a partial line, which is skipped).
// The stats folder lives outside <data_dir>/cache, so "Clear Cache" keeps it.
//
// Time accounting (ReadingStats): the time a page stays on screen is credited
// when the reader leaves it, capped at kMaxPageMs so walking away from the
// device doesn't count. A forward page turn after at least kMinPageMs counts
// as one page read (fast skimming / held buttons don't).
//
// All dates are local dates. The device clock holds local wall time directly
// (see IRuntime::local_time()); no time-zone handling happens anywhere.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace microreader {

class IRuntime;

// ── Calendar helpers (proleptic Gregorian) ──────────────────────────────────

struct CivilDate {
  int year = 1970;
  int month = 1;  // 1..12
  int day = 1;    // 1..31

  bool operator==(const CivilDate& o) const {
    return year == o.year && month == o.month && day == o.day;
  }
  bool operator!=(const CivilDate& o) const {
    return !(*this == o);
  }
};

// Days since 1970-01-01 (negative before). Valid for any int year.
int64_t days_from_civil(int year, int month, int day);
inline int64_t days_from_civil(const CivilDate& d) {
  return days_from_civil(d.year, d.month, d.day);
}
CivilDate civil_from_days(int64_t days);

// Local seconds since epoch → local calendar date / time of day.
CivilDate date_from_local_time(int64_t t);
int seconds_of_day(int64_t t);  // 0..86399

bool is_leap_year(int year);
int days_in_month(int year, int month);
int weekday_monday0(const CivilDate& d);  // 0 = Monday … 6 = Sunday

const char* month_name(int month);     // "January"
const char* month_abbrev(int month);   // "Jan"
const char* weekday_name(int wd);      // wd: 0 = Monday → "Monday"
const char* weekday_abbrev(int wd);    // "Mon"

// A wall clock reading below this (2024-01-01) means "never set".
constexpr int64_t kClockValidAfter = 1704067200;
inline bool clock_is_valid(int64_t local_seconds) {
  return local_seconds >= kClockValidAfter;
}

// Compile date/time of the firmware, as local seconds since epoch. Used as a
// last-resort starting point for a device clock that has never been set.
int64_t firmware_build_time();

// "45m", "1h 5m", "2h" (compact=false) or "45m", "1h05", "2h" (compact=true).
// Non-zero durations under a minute show as "<1m".
std::string format_duration(uint32_t seconds, bool compact = false);

// ── Stored data ─────────────────────────────────────────────────────────────

struct DayTotals {
  uint32_t seconds = 0;
  uint32_t pages = 0;

  bool any() const {
    return seconds > 0 || pages > 0;
  }
};

// Aggregated contents of one month's stats file.
struct MonthStats {
  static constexpr size_t kMaxTitles = 48;     // further titles fold into "Other books"
  static constexpr size_t kMaxBookDays = 512;  // per-(day, book) detail rows kept

  struct BookDay {
    uint8_t day = 0;     // 1..31
    uint16_t title = 0;  // index into titles
    uint32_t seconds = 0;
    uint32_t pages = 0;
  };

  int year = 0;
  int month = 0;
  DayTotals days[31] = {};
  std::vector<std::string> titles;
  std::vector<BookDay> books;  // aggregated per (day, title), only when loaded with_books

  void clear();
  // Loads <stats_dir>/YYYY-MM.txt. A missing file is an empty month.
  // Returns true if the file existed.
  bool load(const std::string& stats_dir, int year, int month, bool with_books = true);

  const DayTotals& day(int d) const {
    return days[(d >= 1 && d <= 31) ? d - 1 : 0];
  }
  uint32_t total_seconds() const;
  uint32_t total_pages() const;
  int days_read() const;
  int longest_streak() const;  // within this month only
};

// ── Recorder ────────────────────────────────────────────────────────────────

class ReadingStats {
 public:
  static constexpr uint32_t kMaxPageMs = 3u * 60u * 1000u;      // idle cap per page
  static constexpr uint32_t kMinPageMs = 2000u;                 // dwell for a page to count as read
  static constexpr uint32_t kFlushEveryMs = 10u * 60u * 1000u;  // bound on data lost to a power cut
  static constexpr size_t kMaxTitleBytes = 80;
  static constexpr int kMaxStreakDays = 1000;

  // Stats are written to <dir>/stats. nullptr/empty disables writing.
  void set_data_dir(const char* dir);
  const std::string& stats_dir() const {
    return stats_dir_;
  }

  // Clock source (normally the platform runtime). nullptr disables recording.
  void set_clock(IRuntime* runtime) {
    clock_ = runtime;
  }
  IRuntime* clock() const {
    return clock_;
  }

  // Today's local date. False if there is no valid wall clock.
  bool today(CivilDate& out) const;

  // ── Reader hooks ──
  void book_opened(std::string_view title);  // starts timing the first page
  void page_turned(bool forward);            // credits the page just left
  void reading_paused();                     // a child screen covers the reader
  void reading_resumed();
  void book_closed();  // credits the current page and flushes
  void flush();        // append pending reading to the month file

  // Consecutive days with reading ending today — or yesterday, if nothing has
  // been read yet today. Reads the month files on demand.
  int current_streak(const CivilDate& today) const;

  // ── File helpers (public for tests/tools) ──
  static std::string month_path(const std::string& stats_dir, int year, int month);
  static bool append_record(const std::string& stats_dir, const CivilDate& date, uint32_t seconds, uint32_t pages,
                            std::string_view title);
  static std::string sanitize_title(std::string_view title);

  // Introspection for tests.
  uint32_t pending_ms() const {
    return pending_ms_;
  }
  uint32_t pending_pages() const {
    return pending_pages_;
  }
  bool is_timing() const {
    return timing_;
  }

 private:
  void start_interval_();
  void close_interval_(bool forward);

  std::string stats_dir_;
  IRuntime* clock_ = nullptr;
  std::string title_;
  bool book_open_ = false;
  bool timing_ = false;
  uint32_t interval_start_ms_ = 0;
  uint32_t pending_ms_ = 0;
  uint32_t pending_pages_ = 0;
  CivilDate pending_date_;
  bool pending_has_date_ = false;
};

}  // namespace microreader
