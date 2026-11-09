#include "StatsCalendarScreen.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

#include "../Application.h"
#include "ListMenuScreen.h"

namespace microreader {

namespace {

using UiFont = ListMenuScreen::UiFont;

const BitmapFont& ui(UiFont f) {
  return ListMenuScreen::shared_font(f);
}

int text_w(const BitmapFont& f, std::string_view s) {
  return f.word_width(s.data(), s.size(), FontStyle::Regular);
}

// Draw text with its top edge at `top`.
void text_at(DrawBuffer& buf, const BitmapFont& f, int x, int top, std::string_view s, bool white = false) {
  buf.draw_text_proportional(x, top + f.baseline(), s.data(), s.size(), f, white);
}

void text_center(DrawBuffer& buf, const BitmapFont& f, int cx, int top, std::string_view s, bool white = false) {
  text_at(buf, f, cx - text_w(f, s) / 2, top, s, white);
}

void text_right(DrawBuffer& buf, const BitmapFont& f, int right, int top, std::string_view s, bool white = false) {
  text_at(buf, f, right - text_w(f, s), top, s, white);
}

// Draw text clipped to max_w, ending in "..." when it doesn't fit.
void text_fit(DrawBuffer& buf, const BitmapFont& f, int x, int top, std::string_view s, int max_w) {
  if (text_w(f, s) <= max_w) {
    text_at(buf, f, x, top, s);
    return;
  }
  static const char kEllipsis[] = "...";
  const int budget = max_w - text_w(f, kEllipsis);
  size_t fit = 0;
  while (fit < s.size()) {
    const uint8_t b = static_cast<uint8_t>(s[fit]);
    const size_t cb = b < 0x80 ? 1u : b < 0xE0 ? 2u : b < 0xF0 ? 3u : 4u;
    if (fit + cb > s.size() || text_w(f, s.substr(0, fit + cb)) > budget)
      break;
    fit += cb;
  }
  std::string t(s.substr(0, fit));
  t += kEllipsis;
  text_at(buf, f, x, top, t);
}

// Rectangle outline of thickness t.
void frame(DrawBuffer& buf, int x, int y, int w, int h, int t, bool white) {
  buf.fill_rect(x, y, w, t, white);
  buf.fill_rect(x, y + h - t, w, t, white);
  buf.fill_rect(x, y + t, t, h - 2 * t, white);
  buf.fill_rect(x + w - t, y + t, t, h - 2 * t, white);
}

std::string pages_text(uint32_t pages) {
  char b[24];
  std::snprintf(b, sizeof(b), "%u page%s", static_cast<unsigned>(pages), pages == 1 ? "" : "s");
  return b;
}

std::string days_text(int days) {
  char b[24];
  std::snprintf(b, sizeof(b), "%d day%s", days, days == 1 ? "" : "s");
  return b;
}

}  // namespace

// ── Lifecycle ───────────────────────────────────────────────────────────────

void StatsCalendarScreen::start(DrawBuffer& buf, IRuntime& runtime) {
  stats_dir_ = app_ ? app_->stats().stats_dir() : std::string();
  if (app_)
    app_->stats().flush();  // make sure nothing read so far is missing

  const int64_t now = runtime.local_time();
  have_today_ = clock_is_valid(now);
  const int64_t ref = have_today_ ? now : firmware_build_time();
  today_ = date_from_local_time(ref);
  now_seconds_of_day_ = seconds_of_day(ref);

  view_year_ = today_.year;
  view_month_ = today_.month;
  sel_day_ = today_.day;
  streak_ = (have_today_ && app_) ? app_->stats().current_streak(today_) : 0;
  load_month_();
  draw_(buf, runtime.battery_percentage());
}

void StatsCalendarScreen::stop() {
  month_.clear();
  { std::string tmp; stats_dir_.swap(tmp); }
}

void StatsCalendarScreen::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) {
  // Front ▲/▼ follow the Menu Nav setting exactly like ListMenuScreen.
  const bool inv_menu = app_ && app_->invert_menu_buttons();
  const Button prev_front = inv_menu ? Button::Button2 : Button::Button3;
  const Button next_front = inv_menu ? Button::Button3 : Button::Button2;

  bool changed = false;
  bool had_prev = false;
  bool had_next = false;
  Button btn;
  while (buttons.next_press(btn)) {
    if (btn == prev_front) {
      move_day_(-1);
      changed = had_prev = true;
    } else if (btn == next_front) {
      move_day_(+1);
      changed = had_next = true;
    } else if (btn == Button::Up) {
      move_month_(-1);
      changed = true;
    } else if (btn == Button::Down) {
      move_month_(+1);
      changed = true;
    } else if (btn == Button::Button1) {
      if (app_) {
        app_->push_screen(ScreenId::DateTime);
        return;
      }
    } else if (btn == Button::Button0) {
      if (app_) {
        app_->pop_screen();
        return;
      }
    }
  }
  // Held front button: one day per frame (no fresh press this frame).
  if (!had_prev && !had_next) {
    if (buttons.is_down(prev_front)) {
      move_day_(-1);
      changed = true;
    } else if (buttons.is_down(next_front)) {
      move_day_(+1);
      changed = true;
    }
  }

  if (changed) {
    draw_(buf, runtime.battery_percentage());
    buf.refresh();
  }
}

// ── Navigation ──────────────────────────────────────────────────────────────

void StatsCalendarScreen::load_month_() {
  month_.load(stats_dir_, view_year_, view_month_, /*with_books=*/true);
}

void StatsCalendarScreen::move_day_(int delta) {
  const CivilDate d = civil_from_days(days_from_civil(view_year_, view_month_, sel_day_) + delta);
  const bool month_changed = d.year != view_year_ || d.month != view_month_;
  view_year_ = d.year;
  view_month_ = d.month;
  sel_day_ = d.day;
  if (month_changed)
    load_month_();
}

void StatsCalendarScreen::move_month_(int delta) {
  int m = view_month_ - 1 + delta;
  int y = view_year_ + (m >= 0 ? m / 12 : -((11 - m) / 12));
  m = ((m % 12) + 12) % 12 + 1;
  view_year_ = y;
  view_month_ = m;
  sel_day_ = std::min(sel_day_, days_in_month(y, m));
  load_month_();
}

bool StatsCalendarScreen::is_today_(int day) const {
  return have_today_ && today_.year == view_year_ && today_.month == view_month_ && today_.day == day;
}

bool StatsCalendarScreen::is_future_(int day) const {
  if (!have_today_)
    return false;
  return days_from_civil(view_year_, view_month_, day) > days_from_civil(today_);
}

int StatsCalendarScreen::days_elapsed_in_view_() const {
  const int dim = days_in_month(view_year_, view_month_);
  if (!have_today_)
    return dim;
  if (view_year_ == today_.year && view_month_ == today_.month)
    return today_.day;
  return days_from_civil(view_year_, view_month_, 1) > days_from_civil(today_) ? 0 : dim;
}

// ── Drawing ─────────────────────────────────────────────────────────────────

StatsCalendarScreen::Layout StatsCalendarScreen::layout_(const DrawBuffer& buf) const {
  Layout L;
  const int W = buf.width();
  const int H = buf.height();
  L.landscape = W > H;
  if (!L.landscape) {
    // Portrait 480 x 786: header, 7x6 grid, info block, menu bottom bar.
    L.content_w = W;
    L.title_y = 15;
    L.month_y = 54;
    L.month_cx = W / 2;
    L.wd_y = 88;
    L.cell_w = (W - 32) / 7;
    L.cell_h = 56;
    L.grid_x = (W - L.cell_w * 7) / 2;
    L.grid_y = 112;
    L.info_x = L.grid_x;
    L.info_w = L.cell_w * 7;
    L.info_y = L.grid_y + L.cell_h * 6 + 14;
    L.info_bottom = H - 26;  // menu bottom bar reserves the last ~19 px
  } else {
    // Landscape 786 x 480: grid left, info panel right; the button hints run
    // down the right edge, so keep clear of the last ~24 px.
    L.content_w = W - 24;
    L.title_y = 12;
    L.cell_w = 56;
    L.cell_h = 58;
    L.grid_x = 16;
    L.month_y = 50;
    L.month_cx = L.grid_x + L.cell_w * 7 / 2;
    L.wd_y = 82;
    L.grid_y = 105;
    L.info_x = L.grid_x + L.cell_w * 7 + 24;
    L.info_w = L.content_w - L.info_x - 8;
    L.info_y = L.month_y;
    L.info_bottom = H - 12;
  }
  return L;
}

void StatsCalendarScreen::draw_(DrawBuffer& buf, std::optional<uint8_t> battery) const {
  const Layout L = layout_(buf);
  buf.fill(true);

  const BitmapFont& header = ui(UiFont::Header);
  const BitmapFont& large = ui(UiFont::Large);
  const BitmapFont& small = ui(UiFont::Small);

  text_center(buf, header, L.content_w / 2, L.title_y, "Reading Stats");

  char month_label[32];
  std::snprintf(month_label, sizeof(month_label), "%s %d", month_name(view_month_), view_year_);
  text_center(buf, large, L.month_cx, L.month_y, month_label);

  // Weekday header + rule.
  for (int c = 0; c < 7; ++c)
    text_center(buf, small, L.grid_x + c * L.cell_w + L.cell_w / 2, L.wd_y, weekday_abbrev(c));
  buf.fill_rect(L.grid_x, L.wd_y + small.y_advance() + 4, L.cell_w * 7, 1, false);

  draw_grid_(buf, L);
  draw_info_(buf, L);

  ListMenuScreen::draw_menu_bottom_bar(buf, ListMenuScreen::shared_menu_font(), app_ && app_->invert_menu_buttons(),
                                       battery);
}

void StatsCalendarScreen::draw_grid_(DrawBuffer& buf, const Layout& L) const {
  const int first_wd = weekday_monday0(CivilDate{view_year_, view_month_, 1});
  const int dim = days_in_month(view_year_, view_month_);
  for (int day = 1; day <= dim; ++day) {
    const int slot = first_wd + day - 1;
    const int x = L.grid_x + (slot % 7) * L.cell_w;
    const int y = L.grid_y + (slot / 7) * L.cell_h;
    draw_cell_(buf, x, y, L.cell_w, L.cell_h, day);
  }
}

void StatsCalendarScreen::draw_cell_(DrawBuffer& buf, int x, int y, int w, int h, int day) const {
  const BitmapFont& medium = ui(UiFont::Medium);
  const BitmapFont& small = ui(UiFont::Small);
  const DayTotals& t = month_.day(day);
  const bool selected = day == sel_day_;
  const bool today = is_today_(day);
  const bool ink = selected;  // selected cell is drawn inverted (white on black)

  if (selected) {
    buf.fill_rect(x + 2, y + 2, w - 4, h - 4, false);
    if (today)
      frame(buf, x + 5, y + 5, w - 10, h - 10, 1, true);
  } else if (today) {
    frame(buf, x + 2, y + 2, w - 4, h - 4, 2, false);
  }

  char num[12];
  std::snprintf(num, sizeof(num), "%d", day);
  const int num_y = y + 4;
  text_center(buf, medium, x + w / 2, num_y, num, ink);

  if (!t.any())
    return;

  const std::string dur = format_duration(t.seconds, /*compact=*/true);
  const int dur_y = num_y + medium.y_advance();
  text_center(buf, small, x + w / 2, dur_y, dur, ink);

  // Progress bar toward kFullBarSeconds, below the duration with a small gap.
  const int bar_x = x + 8;
  const int bar_w = w - 16;
  const int bar_h = 6;
  const int bar_y = std::min(dur_y + small.y_advance() + 3, y + h - 4 - bar_h - 2);
  frame(buf, bar_x, bar_y, bar_w, bar_h, 1, ink);
  const uint32_t capped = std::min<uint32_t>(t.seconds, kFullBarSeconds);
  int fill = static_cast<int>((bar_w - 2) * static_cast<int64_t>(capped) / kFullBarSeconds);
  if (fill < 1)
    fill = 1;  // any reading at all shows a sliver
  buf.fill_rect(bar_x + 1, bar_y + 1, fill, bar_h - 2, ink);
}

void StatsCalendarScreen::draw_info_(DrawBuffer& buf, const Layout& L) const {
  const BitmapFont& medium = ui(UiFont::Medium);
  const BitmapFont& large = ui(UiFont::Large);
  const BitmapFont& small = ui(UiFont::Small);
  const int x = L.info_x;
  const int right = L.info_x + L.info_w;
  int y = L.info_y;

  // ── Selected day ──
  const CivilDate sel{view_year_, view_month_, sel_day_};
  char heading[48];
  if (is_today_(sel_day_))
    std::snprintf(heading, sizeof(heading), "Today, %s %d %s", weekday_abbrev(weekday_monday0(sel)), sel_day_,
                  month_abbrev(view_month_));
  else
    std::snprintf(heading, sizeof(heading), "%s, %d %s", weekday_name(weekday_monday0(sel)), sel_day_,
                  month_name(view_month_));
  text_at(buf, medium, x, y, heading);
  if (!have_today_) {
    text_right(buf, small, right, y + 2, "Clock not set");
  } else if (is_today_(sel_day_)) {
    char clock[16];
    std::snprintf(clock, sizeof(clock), "%02d:%02d", now_seconds_of_day_ / 3600, (now_seconds_of_day_ / 60) % 60);
    text_right(buf, small, right, y + 2, clock);
  }
  y += medium.y_advance() + 6;

  const DayTotals& t = month_.day(sel_day_);
  if (t.any()) {
    const std::string line = format_duration(t.seconds) + " \xc2\xb7 " + pages_text(t.pages);
    text_at(buf, large, x, y, line);
  } else {
    text_at(buf, large, x, y, is_future_(sel_day_) ? "\xe2\x80\x94" : "No reading");
  }
  y += large.y_advance() + 6;

  // Books read that day, longest first.
  const int line_h = small.y_advance() + 4;
  const int max_lines = L.landscape ? 5 : 3;
  int shown = 0, total_books = 0;
  {
    std::vector<const MonthStats::BookDay*> order;
    for (const auto& b : month_.books)
      if (b.day == sel_day_)
        order.push_back(&b);
    std::sort(order.begin(), order.end(),
              [](const MonthStats::BookDay* a, const MonthStats::BookDay* b) { return a->seconds > b->seconds; });
    const int n = static_cast<int>(order.size());
    total_books = n;
    const int lines = (n > max_lines) ? max_lines - 1 : n;  // keep a line for "+N more"
    for (int i = 0; i < lines; ++i) {
      const std::string dur = format_duration(order[i]->seconds);
      const int dur_w = text_w(small, dur);
      const std::string& title = month_.titles[order[i]->title];
      text_at(buf, small, x, y, "\xe2\x80\xa2");
      text_fit(buf, small, x + 14, y, title, L.info_w - 14 - dur_w - 12);
      text_right(buf, small, right, y, dur);
      y += line_h;
      ++shown;
    }
  }
  if (total_books > shown) {
    char more[32];
    std::snprintf(more, sizeof(more), "+%d more book%s", total_books - shown, total_books - shown == 1 ? "" : "s");
    text_at(buf, small, x + 14, y, more);
    y += line_h;
  }
  // Reserve the book-line block so the summary below doesn't jump around
  // as the selection moves between days.
  y = L.info_y + medium.y_advance() + 6 + large.y_advance() + 6 + line_h * max_lines;

  // ── Month summary ──
  y += 6;
  buf.fill_rect(x, y, L.info_w, 1, false);
  y += 10;

  const uint32_t total = month_.total_seconds();
  const int elapsed = days_elapsed_in_view_();
  char days_read[32];
  std::snprintf(days_read, sizeof(days_read), "%d / %d", month_.days_read(), days_in_month(view_year_, view_month_));
  const std::string avg = elapsed > 0 ? format_duration(total / static_cast<uint32_t>(elapsed)) : "\xe2\x80\x94";
  const std::string streak = have_today_ ? days_text(streak_) : "\xe2\x80\x94";

  struct Stat {
    const char* label;
    std::string value;
  };
  const Stat stats[6] = {
      {"MONTH TOTAL", format_duration(total)},
      {"DAYS READ", days_read},
      {"PAGES", std::to_string(month_.total_pages())},
      {"DAILY AVERAGE", avg},
      {"CURRENT STREAK", streak},
      {"LONGEST STREAK", days_text(month_.longest_streak())},
  };
  const int col_w = L.info_w / 2;
  const int row_h = small.y_advance() + 2 + large.y_advance() + 8;
  for (int i = 0; i < 6; ++i) {
    const int cx = x + (i % 2) * col_w;
    const int cy = y + (i / 2) * row_h;
    if (cy + row_h - 8 > L.info_bottom)
      break;
    text_at(buf, small, cx, cy, stats[i].label);
    text_at(buf, large, cx, cy + small.y_advance() + 2, stats[i].value);
  }
}

}  // namespace microreader
