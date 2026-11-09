#include "ReadingStats.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../HeapLog.h"
#include "../Runtime.h"

#ifdef ESP_PLATFORM
#include <sys/stat.h>
#else
#include <filesystem>
#endif

namespace microreader {

// ── Calendar helpers ────────────────────────────────────────────────────────
// days_from_civil / civil_from_days: Howard Hinnant's public-domain algorithms
// (http://howardhinnant.github.io/date_algorithms.html).

int64_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);                          // [0, 399]
  const unsigned doy = (153u * (m > 2 ? m - 3 : m + 9) + 2u) / 5u + d - 1u;           // [0, 365]
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;                      // [0, 146096]
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

CivilDate civil_from_days(int64_t z) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);                // [0, 146096]
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0, 399]
  const int64_t y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);  // [0, 365]
  const unsigned mp = (5 * doy + 2) / 153;                       // [0, 11]
  CivilDate out;
  out.day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  out.month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  out.year = static_cast<int>(y + (out.month <= 2));
  return out;
}

static int64_t floor_div(int64_t a, int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0)))
    --q;
  return q;
}

CivilDate date_from_local_time(int64_t t) {
  return civil_from_days(floor_div(t, 86400));
}

int seconds_of_day(int64_t t) {
  return static_cast<int>(t - floor_div(t, 86400) * 86400);
}

bool is_leap_year(int y) {
  return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

int days_in_month(int year, int month) {
  static const uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12)
    return 30;
  return (month == 2 && is_leap_year(year)) ? 29 : kDays[month - 1];
}

int weekday_monday0(const CivilDate& d) {
  // 1970-01-01 was a Thursday (Monday-based index 3).
  const int64_t days = days_from_civil(d);
  int64_t wd = (days + 3) % 7;
  if (wd < 0)
    wd += 7;
  return static_cast<int>(wd);
}

static const char* const kMonthNames[12] = {"January", "February", "March",     "April",   "May",      "June",
                                            "July",    "August",   "September", "October", "November", "December"};
static const char* const kMonthAbbrev[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char* const kWeekdayNames[7] = {"Monday", "Tuesday", "Wednesday", "Thursday",
                                             "Friday", "Saturday", "Sunday"};
static const char* const kWeekdayAbbrev[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

const char* month_name(int month) {
  return (month >= 1 && month <= 12) ? kMonthNames[month - 1] : "?";
}
const char* month_abbrev(int month) {
  return (month >= 1 && month <= 12) ? kMonthAbbrev[month - 1] : "?";
}
const char* weekday_name(int wd) {
  return (wd >= 0 && wd <= 6) ? kWeekdayNames[wd] : "?";
}
const char* weekday_abbrev(int wd) {
  return (wd >= 0 && wd <= 6) ? kWeekdayAbbrev[wd] : "?";
}

int64_t firmware_build_time() {
  // __DATE__ = "Mmm dd yyyy", __TIME__ = "hh:mm:ss" (local time of the build machine).
  static const char kDate[] = __DATE__;
  static const char kTime[] = __TIME__;
  int month = 1;
  for (int i = 0; i < 12; ++i) {
    if (std::strncmp(kDate, kMonthAbbrev[i], 3) == 0) {
      month = i + 1;
      break;
    }
  }
  const int day = std::atoi(kDate + 4);
  const int year = std::atoi(kDate + 7);
  const int hh = std::atoi(kTime);
  const int mm = std::atoi(kTime + 3);
  const int ss = std::atoi(kTime + 6);
  return days_from_civil(year, month, day > 0 ? day : 1) * 86400 + hh * 3600 + mm * 60 + ss;
}

std::string format_duration(uint32_t seconds, bool compact) {
  if (seconds == 0)
    return "0m";
  const uint32_t minutes = (seconds + 30) / 60;
  if (minutes == 0)
    return "<1m";
  char buf[24];
  const unsigned h = static_cast<unsigned>(minutes / 60);
  const unsigned m = static_cast<unsigned>(minutes % 60);
  if (h == 0)
    std::snprintf(buf, sizeof(buf), "%um", m);
  else if (m == 0)
    std::snprintf(buf, sizeof(buf), "%uh", h);
  else if (compact)
    std::snprintf(buf, sizeof(buf), "%uh%02u", h, m);
  else
    std::snprintf(buf, sizeof(buf), "%uh %um", h, m);
  return buf;
}

// ── File helpers ────────────────────────────────────────────────────────────

static bool ensure_dir(const std::string& dir) {
  if (dir.empty())
    return false;
#ifdef ESP_PLATFORM
  mkdir(dir.c_str(), 0775);  // EEXIST is fine; fopen below reports real failures
  return true;
#else
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return !ec;
#endif
}

std::string ReadingStats::month_path(const std::string& stats_dir, int year, int month) {
  char name[24];
  std::snprintf(name, sizeof(name), "/%04d-%02d.txt", year, month);
  return stats_dir + name;
}

std::string ReadingStats::sanitize_title(std::string_view title) {
  std::string out;
  out.reserve(title.size() < kMaxTitleBytes ? title.size() : kMaxTitleBytes);
  for (char c : title)
    out.push_back(static_cast<unsigned char>(c) < 0x20 ? ' ' : c);
  // Trim surrounding spaces.
  size_t b = 0;
  while (b < out.size() && out[b] == ' ')
    ++b;
  size_t e = out.size();
  while (e > b && out[e - 1] == ' ')
    --e;
  out = out.substr(b, e - b);
  // Truncate on a UTF-8 character boundary.
  if (out.size() > kMaxTitleBytes) {
    size_t cut = kMaxTitleBytes;
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
      --cut;
    out.resize(cut);
  }
  return out;
}

bool ReadingStats::append_record(const std::string& stats_dir, const CivilDate& date, uint32_t seconds, uint32_t pages,
                                 std::string_view title) {
  if (stats_dir.empty() || (seconds == 0 && pages == 0))
    return false;
  ensure_dir(stats_dir);
  const std::string path = month_path(stats_dir, date.year, date.month);
  bool exists = false;
  if (FILE* probe = std::fopen(path.c_str(), "r")) {
    exists = true;
    std::fclose(probe);
  }
  FILE* f = std::fopen(path.c_str(), "a");
  if (!f) {
    MR_LOGI("stats", "cannot open %s for append", path.c_str());
    return false;
  }
  if (!exists)
    std::fputs("# micropow reading stats v1: <day> <seconds> <pages> <title>\n", f);
  std::string t = sanitize_title(title);
  if (t.empty())
    t = "Unknown book";
  std::fprintf(f, "%u %u %u %s\n", static_cast<unsigned>(date.day), static_cast<unsigned>(seconds),
               static_cast<unsigned>(pages), t.c_str());
  std::fclose(f);
  return true;
}

// ── MonthStats ──────────────────────────────────────────────────────────────

void MonthStats::clear() {
  year = month = 0;
  for (auto& d : days)
    d = DayTotals{};
  { std::vector<std::string> tmp; titles.swap(tmp); }
  { std::vector<BookDay> tmp; books.swap(tmp); }
}

// Parse an unsigned decimal at *p, advancing past it and any following spaces.
static bool parse_uint(const char*& p, uint32_t& out) {
  if (*p < '0' || *p > '9')
    return false;
  uint64_t v = 0;
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + static_cast<uint64_t>(*p - '0');
    if (v > 0xFFFFFFFFull)
      return false;
    ++p;
  }
  while (*p == ' ' || *p == '\t')
    ++p;
  out = static_cast<uint32_t>(v);
  return true;
}

bool MonthStats::load(const std::string& stats_dir, int y, int m, bool with_books) {
  clear();
  year = y;
  month = m;
  if (stats_dir.empty())
    return false;
  const std::string path = ReadingStats::month_path(stats_dir, y, m);
  FILE* f = std::fopen(path.c_str(), "r");
  if (!f)
    return false;

  const int dim = days_in_month(y, m);
  char line[192];
  while (std::fgets(line, sizeof(line), f)) {
    char* nl = std::strchr(line, '\n');
    if (!nl && !std::feof(f)) {
      // Over-long (corrupt) line: skip the remainder and ignore it.
      int c;
      while ((c = std::fgetc(f)) != EOF && c != '\n') {
      }
      continue;
    }
    if (nl)
      *nl = '\0';
    if (char* cr = std::strchr(line, '\r'))
      *cr = '\0';
    if (line[0] == '#' || line[0] == '\0')
      continue;

    const char* p = line;
    uint32_t d = 0, secs = 0, pages = 0;
    if (!parse_uint(p, d) || !parse_uint(p, secs) || !parse_uint(p, pages))
      continue;
    if (d < 1 || d > static_cast<uint32_t>(dim))
      continue;
    if (secs > 86400u)
      secs = 86400u;  // a single record can't exceed a day; guards against corruption
    if (pages > 100000u)
      continue;

    DayTotals& dt = days[d - 1];
    dt.seconds += secs;
    dt.pages += pages;

    if (!with_books)
      continue;
    std::string_view title(p);
    if (title.empty())
      title = "Unknown book";
    uint16_t ti = 0;
    bool found = false;
    for (size_t i = 0; i < titles.size(); ++i) {
      if (titles[i] == title) {
        ti = static_cast<uint16_t>(i);
        found = true;
        break;
      }
    }
    if (!found) {
      if (titles.size() < kMaxTitles) {
        titles.emplace_back(title);
        ti = static_cast<uint16_t>(titles.size() - 1);
      } else {
        // Fold the overflow into a shared "Other books" bucket.
        static const char kOther[] = "Other books";
        bool have_other = false;
        for (size_t i = kMaxTitles; i < titles.size(); ++i)
          if (titles[i] == kOther) {
            ti = static_cast<uint16_t>(i);
            have_other = true;
          }
        if (!have_other) {
          titles.emplace_back(kOther);
          ti = static_cast<uint16_t>(titles.size() - 1);
        }
      }
    }
    bool merged = false;
    for (auto& b : books) {
      if (b.day == d && b.title == ti) {
        b.seconds += secs;
        b.pages += pages;
        merged = true;
        break;
      }
    }
    if (!merged && books.size() < kMaxBookDays) {
      BookDay b;
      b.day = static_cast<uint8_t>(d);
      b.title = ti;
      b.seconds = secs;
      b.pages = pages;
      books.push_back(b);
    }
  }
  std::fclose(f);
  return true;
}

uint32_t MonthStats::total_seconds() const {
  uint32_t t = 0;
  for (const auto& d : days)
    t += d.seconds;
  return t;
}

uint32_t MonthStats::total_pages() const {
  uint32_t t = 0;
  for (const auto& d : days)
    t += d.pages;
  return t;
}

int MonthStats::days_read() const {
  int n = 0;
  for (const auto& d : days)
    n += d.any() ? 1 : 0;
  return n;
}

int MonthStats::longest_streak() const {
  int best = 0, run = 0;
  for (const auto& d : days) {
    run = d.any() ? run + 1 : 0;
    if (run > best)
      best = run;
  }
  return best;
}

// ── ReadingStats recorder ───────────────────────────────────────────────────

void ReadingStats::set_data_dir(const char* dir) {
  stats_dir_ = (dir && *dir) ? std::string(dir) + "/stats" : std::string();
}

bool ReadingStats::today(CivilDate& out) const {
  if (!clock_)
    return false;
  const int64_t t = clock_->local_time();
  if (!clock_is_valid(t))
    return false;
  out = date_from_local_time(t);
  return true;
}

void ReadingStats::start_interval_() {
  if (!clock_ || !book_open_)
    return;
  interval_start_ms_ = clock_->monotonic_ms();
  timing_ = true;
}

void ReadingStats::close_interval_(bool forward) {
  if (!timing_)
    return;
  timing_ = false;
  if (!clock_)
    return;
  uint32_t dt = clock_->monotonic_ms() - interval_start_ms_;  // unsigned: wrap-safe
  if (dt > kMaxPageMs)
    dt = kMaxPageMs;

  CivilDate d;
  if (!today(d))
    return;  // no wall clock: we can't date the reading, so don't record it
  if (pending_has_date_ && d != pending_date_)
    flush();  // day rolled over (or the clock was changed): close out the old day
  pending_date_ = d;
  pending_has_date_ = true;
  pending_ms_ += dt;
  if (forward && dt >= kMinPageMs)
    ++pending_pages_;
}

void ReadingStats::book_opened(std::string_view title) {
  if (book_open_)
    book_closed();
  title_ = sanitize_title(title);
  book_open_ = true;
  start_interval_();
}

void ReadingStats::page_turned(bool forward) {
  if (!book_open_)
    return;
  close_interval_(forward);
  if (pending_ms_ >= kFlushEveryMs)
    flush();
  start_interval_();
}

void ReadingStats::reading_paused() {
  close_interval_(false);
}

void ReadingStats::reading_resumed() {
  if (book_open_ && !timing_)
    start_interval_();
}

void ReadingStats::book_closed() {
  if (!book_open_)
    return;
  close_interval_(false);
  flush();
  book_open_ = false;
  title_.clear();
}

void ReadingStats::flush() {
  if (!pending_has_date_)
    return;
  const uint32_t secs = pending_ms_ / 1000u;
  if (secs == 0 && pending_pages_ == 0)
    return;
  if (!append_record(stats_dir_, pending_date_, secs, pending_pages_, title_))
    MR_LOGI("stats", "flush dropped %us / %u pages (no stats dir?)", static_cast<unsigned>(secs),
            static_cast<unsigned>(pending_pages_));
  pending_ms_ %= 1000u;  // keep the sub-second remainder for the next record
  pending_pages_ = 0;
}

int ReadingStats::current_streak(const CivilDate& today) const {
  if (stats_dir_.empty())
    return 0;
  MonthStats m;
  int loaded_y = 0, loaded_m = 0;
  int64_t day = days_from_civil(today);
  int streak = 0;
  for (int i = 0; i < kMaxStreakDays; ++i, --day) {
    const CivilDate d = civil_from_days(day);
    if (d.year != loaded_y || d.month != loaded_m) {
      const bool existed = m.load(stats_dir_, d.year, d.month, /*with_books=*/false);
      loaded_y = d.year;
      loaded_m = d.month;
      if (!existed && i > 0)
        break;  // a month with no file at all ends any streak
    }
    if (!m.day(d.day).any()) {
      if (i == 0)
        continue;  // nothing yet today: the streak can still run through yesterday
      break;
    }
    ++streak;
  }
  return streak;
}

}  // namespace microreader
