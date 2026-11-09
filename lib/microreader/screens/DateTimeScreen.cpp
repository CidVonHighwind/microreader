#include "DateTimeScreen.h"

#include <cstdio>
#include <cstring>
#include <string_view>

#include "../Application.h"
#include "../HeapLog.h"
#include "../stats/ReadingStats.h"
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

void text_center(DrawBuffer& buf, const BitmapFont& f, int cx, int top, std::string_view s, bool white = false) {
  buf.draw_text_proportional(cx - text_w(f, s) / 2, top + f.baseline(), s.data(), s.size(), f, white);
}

int wrap(int v, int lo, int hi) {
  const int span = hi - lo + 1;
  return lo + (((v - lo) % span) + span) % span;
}

}  // namespace

void DateTimeScreen::start(DrawBuffer& buf, IRuntime& runtime) {
  int64_t now = runtime.local_time();
  if (!clock_is_valid(now))
    now = firmware_build_time();
  const CivilDate d = date_from_local_time(now);
  const int sod = seconds_of_day(now);
  values_[kDay] = d.day;
  values_[kMonth] = d.month;
  values_[kYear] = d.year < kMinYear ? kMinYear : (d.year > kMaxYear ? kMaxYear : d.year);
  values_[kHour] = sod / 3600;
  values_[kMinute] = (sod / 60) % 60;
  clamp_day_();
  field_ = kDay;
  save_failed_ = false;
  draw_(buf, runtime.battery_percentage());
}

int64_t DateTimeScreen::composed_local_time() const {
  return days_from_civil(values_[kYear], values_[kMonth], values_[kDay]) * 86400 + values_[kHour] * 3600 +
         values_[kMinute] * 60;
}

void DateTimeScreen::clamp_day_() {
  const int dim = days_in_month(values_[kYear], values_[kMonth]);
  if (values_[kDay] > dim)
    values_[kDay] = dim;
  if (values_[kDay] < 1)
    values_[kDay] = 1;
}

void DateTimeScreen::adjust_(int delta) {
  switch (field_) {
    case kDay:
      values_[kDay] = wrap(values_[kDay] + delta, 1, days_in_month(values_[kYear], values_[kMonth]));
      break;
    case kMonth:
      values_[kMonth] = wrap(values_[kMonth] + delta, 1, 12);
      clamp_day_();
      break;
    case kYear:
      values_[kYear] = wrap(values_[kYear] + delta, kMinYear, kMaxYear);
      clamp_day_();
      break;
    case kHour:
      values_[kHour] = wrap(values_[kHour] + delta, 0, 23);
      break;
    case kMinute:
      values_[kMinute] = wrap(values_[kMinute] + delta, 0, 59);
      break;
    default:
      break;
  }
}

void DateTimeScreen::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) {
  const bool inv_menu = app_ && app_->invert_menu_buttons();
  const Button up_front = inv_menu ? Button::Button2 : Button::Button3;
  const Button down_front = inv_menu ? Button::Button3 : Button::Button2;

  bool changed = false;
  bool had_up = false, had_down = false;
  Button btn;
  while (buttons.next_press(btn)) {
    if (btn == up_front || btn == Button::Up) {
      adjust_(+1);
      changed = had_up = true;
    } else if (btn == down_front || btn == Button::Down) {
      adjust_(-1);
      changed = had_down = true;
    } else if (btn == Button::Button1) {
      if (field_ + 1 < kNumFields) {
        ++field_;
        changed = true;
      } else {
        // Last field: save.
        if (runtime.set_local_time(composed_local_time())) {
          MR_LOGI("clock", "set to %04d-%02d-%02d %02d:%02d", values_[kYear], values_[kMonth], values_[kDay],
                  values_[kHour], values_[kMinute]);
          if (app_) {
            app_->save_settings_();  // persists the clock across a cold power-on
            app_->pop_screen();
          }
          return;
        }
        save_failed_ = true;
        changed = true;
      }
    } else if (btn == Button::Button0) {
      if (field_ > 0 && !save_failed_) {
        --field_;
        changed = true;
      } else if (app_) {
        app_->pop_screen();  // cancel
        return;
      }
    }
  }

  // Held ▲/▼: repeat once per frame.
  if (!had_up && !had_down) {
    if (buttons.is_down(up_front) || buttons.is_down(Button::Up)) {
      adjust_(+1);
      changed = true;
    } else if (buttons.is_down(down_front) || buttons.is_down(Button::Down)) {
      adjust_(-1);
      changed = true;
    }
  }

  if (changed) {
    draw_(buf, runtime.battery_percentage());
    buf.refresh();
  }
}

void DateTimeScreen::draw_(DrawBuffer& buf, std::optional<uint8_t> battery) const {
  const int W = buf.width();
  const int H = buf.height();
  const bool landscape = W > H;
  const int cx = landscape ? (W - 24) / 2 : W / 2;
  buf.fill(true);

  const BitmapFont& header = ui(UiFont::Header);
  const BitmapFont& medium = ui(UiFont::Medium);
  const BitmapFont& small = ui(UiFont::Small);

  text_center(buf, header, cx, landscape ? 12 : 15, "Date & Time");
  text_center(buf, small, cx, landscape ? 52 : 56, "Used to date your reading statistics");

  // Field boxes: date row "DD  Mon  YYYY", time row "HH : MM".
  char txt[kNumFields][16];
  std::snprintf(txt[kDay], sizeof(txt[kDay]), "%02d", values_[kDay]);
  std::snprintf(txt[kMonth], sizeof(txt[kMonth]), "%s", month_abbrev(values_[kMonth]));
  std::snprintf(txt[kYear], sizeof(txt[kYear]), "%04d", values_[kYear]);
  std::snprintf(txt[kHour], sizeof(txt[kHour]), "%02d", values_[kHour]);
  std::snprintf(txt[kMinute], sizeof(txt[kMinute]), "%02d", values_[kMinute]);

  const int pad_x = 8;
  const int box_h = header.y_advance() + 8;
  auto draw_row = [&](const int* fields, int n, const char* sep, int top) {
    const int sep_w = text_w(header, sep);
    int total = 0;
    for (int i = 0; i < n; ++i)
      total += text_w(header, txt[fields[i]]) + 2 * pad_x + (i > 0 ? sep_w : 0);
    int x = cx - total / 2;
    for (int i = 0; i < n; ++i) {
      if (i > 0) {
        buf.draw_text_proportional(x, top + 4 + header.baseline(), sep, header, false);
        x += sep_w;
      }
      const int f = fields[i];
      const int bw = text_w(header, txt[f]) + 2 * pad_x;
      const bool focused = (f == field_);
      if (focused)
        buf.fill_rect(x, top, bw, box_h, false);
      else
        buf.fill_rect(x + pad_x, top + box_h - 2, bw - 2 * pad_x, 2, false);  // underline
      buf.draw_text_proportional(x + pad_x, top + 4 + header.baseline(), txt[f], header, focused);
      x += bw;
    }
  };

  const int date_y = landscape ? 130 : 230;
  static const int kDateFields[3] = {kDay, kMonth, kYear};
  static const int kTimeFields[2] = {kHour, kMinute};
  draw_row(kDateFields, 3, "  ", date_y);
  draw_row(kTimeFields, 2, " : ", date_y + box_h + 24);

  const CivilDate d{values_[kYear], values_[kMonth], values_[kDay]};
  text_center(buf, medium, cx, date_y + 2 * (box_h + 24) + 8, weekday_name(weekday_monday0(d)));

  // Help.
  const int help_y = landscape ? H - 80 : H - 120;
  if (save_failed_) {
    text_center(buf, medium, cx, help_y - 40, "This device has no settable clock");
  }
  static const char* const kFieldNames[kNumFields] = {"day", "month", "year", "hour", "minute"};
  char line1[64];
  std::snprintf(line1, sizeof(line1), "\xe2\x96\xb2 \xe2\x96\xbc  change %s", kFieldNames[field_]);
  text_center(buf, small, cx, help_y, line1);
  text_center(buf, small, cx, help_y + small.y_advance() + 6,
              field_ + 1 < kNumFields ? "\xe2\x96\xb6  next field" : "\xe2\x96\xb6  save");
  text_center(buf, small, cx, help_y + 2 * (small.y_advance() + 6),
              field_ > 0 ? "\xe2\x97\x80  previous field" : "\xe2\x97\x80  cancel");

  ListMenuScreen::draw_menu_bottom_bar(buf, ListMenuScreen::shared_menu_font(), app_ && app_->invert_menu_buttons(),
                                       battery);
}

}  // namespace microreader
