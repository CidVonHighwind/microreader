#pragma once

#include "../Input.h"
#include "../display/DrawBuffer.h"
#include "ListMenuScreen.h"

namespace microreader {

class SettingsScreen final : public ListMenuScreen {
 public:
  SettingsScreen() = default;

  void set_data_dir(const char* dir) {
    data_dir_ = dir;
  }

  const char* name() const override {
    return "Settings";
  }

  void update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override {
    buf_ = &buf;
    if (toast_frames_ > 0) {
      --toast_frames_;
      if (toast_frames_ == 0 && toast_idx_ >= 0) {
        set_item_label(toast_idx_, toast_original_label_);
        toast_idx_ = -1;
        toast_original_label_.clear();
        request_redraw();
      }
    }
    ListMenuScreen::update(buttons, buf, runtime);
  }

 protected:
  void on_start() override;
  void on_select(int index) override;

 private:
  const char* data_dir_ = nullptr;

  // Item indices (assigned during on_start).
#ifdef MICROREADER_ENABLE_DEMOS
  int idx_bouncing_ball_ = -1;
  int idx_grayscale_demo_ = -1;
#endif

  int idx_stats_ = -1;
  int idx_date_time_ = -1;
  int idx_clear_cache_ = -1;
  int idx_rebuild_index_ = -1;
  int idx_list_format_ = -1;
  int idx_sort_order_ = -1;
  int idx_switch_ota_ = -1;
  int idx_invalidate_font_ = -1;
  int idx_spiffs_ = -1;
  int idx_invert_menu_ = -1;
  int idx_invert_bottom_paging_ = -1;
  int idx_invert_side_ = -1;
  int idx_rotate_display_ = -1;
  int idx_menu_font_ = -1;
  int idx_font_ = -1;
  int idx_sleep_image_ = -1;
  DrawBuffer* buf_ = nullptr;
  std::vector<std::string> sd_fonts_;
  int font_sel_idx_ = 0;
  std::vector<std::string> sleep_images_;
  int sleep_image_sel_idx_ = 0;
  int toast_idx_ = -1;
  std::string toast_original_label_;
  int toast_frames_ = 0;

  void clear_cache_();
#ifdef ESP_PLATFORM
  void switch_ota_partition_();
#endif
};

}  // namespace microreader
