#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "../Input.h"
#include "../display/DrawBuffer.h"
#include "../stats/ReadingStats.h"
#include "IScreen.h"

namespace microreader {

// Month calendar of reading statistics (time + pages per day).
//
// Controls (front buttons follow the "Menu Nav" setting, like every list):
//   ▲ / ▼ (front)   previous / next day (crosses month boundaries)
//   Side Up / Down  previous / next month
//   ▶ (Button1)     set date & time
//   ◀ (Button0)     back
class StatsCalendarScreen final : public IScreen {
 public:
  // A day counts as a "full bar" at this much reading.
  static constexpr uint32_t kFullBarSeconds = 60u * 60u;

  const char* name() const override {
    return "Stats";
  }

  void start(DrawBuffer& buf, IRuntime& runtime) override;
  void stop() override;
  void update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;

  // Test accessors.
  int view_year() const {
    return view_year_;
  }
  int view_month() const {
    return view_month_;
  }
  int selected_day() const {
    return sel_day_;
  }
  int streak() const {
    return streak_;
  }
  const MonthStats& month() const {
    return month_;
  }

 private:
  struct Layout {
    bool landscape = false;
    int content_w = 0;  // width usable for content (excludes landscape hint column)
    int title_y = 0;
    int month_y = 0;
    int month_cx = 0;
    int wd_y = 0;
    int grid_x = 0, grid_y = 0, cell_w = 0, cell_h = 0;
    int info_x = 0, info_y = 0, info_w = 0, info_bottom = 0;
  };

  Layout layout_(const DrawBuffer& buf) const;
  void load_month_();
  void move_day_(int delta);
  void move_month_(int delta);

  void draw_(DrawBuffer& buf, std::optional<uint8_t> battery) const;
  void draw_grid_(DrawBuffer& buf, const Layout& L) const;
  void draw_cell_(DrawBuffer& buf, int x, int y, int w, int h, int day) const;
  void draw_info_(DrawBuffer& buf, const Layout& L) const;

  // 0 = view month is in the future, otherwise the number of days of the
  // view month that have already happened (for the daily average).
  int days_elapsed_in_view_() const;
  bool is_today_(int day) const;
  bool is_future_(int day) const;

  std::string stats_dir_;
  bool have_today_ = false;  // false = no valid wall clock
  CivilDate today_;
  int now_seconds_of_day_ = 0;
  int view_year_ = 1970;
  int view_month_ = 1;
  int sel_day_ = 1;
  int streak_ = 0;
  MonthStats month_;
};

}  // namespace microreader
