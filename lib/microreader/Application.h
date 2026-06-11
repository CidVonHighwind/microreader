#pragma once

#include <cstdint>
#include <functional>

#include "FontManager.h"
#include "Input.h"
#include "Runtime.h"
#include "ScreenManager.h"
#include "display/DrawBuffer.h"
#include "screens/ChapterSelectScreen.h"
#include "screens/DeleteConfirmScreen.h"
#include "screens/IScreen.h"
#include "screens/LinksScreen.h"
#include "screens/MainMenu.h"
#include "screens/ReaderOptionsScreen.h"
#include "screens/ReaderScreen.h"
#include "screens/SettingsScreen.h"
#include "screens/demo/BouncingBallDemo.h"
#include "screens/demo/GrayscaleDemo.h"

namespace microreader {

// All navigable screens in the application.
enum class ScreenId : uint8_t {
  None = 0,
  MainMenu,
  Reader,
  Settings,
  ReaderOptions,
  ChapterSelect,
  Links,
  DeleteConfirm,
  BouncingBall,
  GrayscaleDemo,
};

class Application {
 public:
  Application() = default;

  void set_books_dir(const char* dir) {
    menu_.set_books_dir(dir);
  }

  void set_data_dir(const char* dir) {
    data_dir_ = dir;
    reader_.set_data_dir(dir ? dir : "");
    settings_.set_data_dir(dir);
  }

  // Path to data directory for settings/state persistence
  const char* data_dir_ = nullptr;

  // Path to the single unified settings file (cached after set_data_dir)
  std::string settings_path_;
  // Book path to auto-open on next start() (set by load_settings_)
  std::string pending_book_path_;
  // Screen to auto-open on next start() (set by load_settings_)
  std::string pending_screen_;

  // Save all persistent state to the settings file
  void save_settings_();
  // Load all persistent state from the settings file
  void load_settings_();
  // Common sleep sequence (save state, show sleep image, set running_=false)
  void do_sleep_(DrawBuffer& buf);

  // Font management. set_reader_font() also propagates to the reader screen.
  void set_reader_font(const BitmapFontSet* fonts) {
    reader_font_ = fonts;
    reader_.set_fonts(fonts);
  }
  void set_font_manager(FontManager* fm) {
    font_manager_ = fm;
  }
  FontManager* font_manager() const {
    return font_manager_;
  }

  // Optional callback for "Invalidate Font" in the Settings menu (ESP32 only).
  void set_invalidate_font_fn(std::function<void()> fn) {
    invalidate_font_fn_ = std::move(fn);
  }
  void invalidate_font() {
    if (invalidate_font_fn_)
      invalidate_font_fn_();
  }
  bool has_invalidate_font_fn() const {
    return static_cast<bool>(invalidate_font_fn_);
  }

  ReaderScreen* reader() {
    return &reader_;
  }
  SettingsScreen* settings() {
    return &settings_;
  }
  ReaderOptionsScreen* reader_options() {
    return &reader_options_;
  }
  ChapterSelectScreen* chapter_select() {
    return &chapter_select_;
  }
  LinksScreen* links_screen() {
    return &links_screen_;
  }
  MainMenu* main_menu() {
    return &menu_;
  }
  DeleteConfirmScreen* delete_confirm() {
    return &delete_confirm_;
  }

  bool invert_menu_buttons() const {
    return invert_menu_buttons_;
  }
  void set_invert_menu_buttons(bool v) {
    invert_menu_buttons_ = v;
  }

  bool invert_bottom_paging() const {
    return invert_bottom_paging_;
  }
  void set_invert_bottom_paging(bool v) {
    invert_bottom_paging_ = v;
  }

  bool invert_side_buttons() const {
    return invert_side_buttons_;
  }
  void set_invert_side_buttons(bool v) {
    invert_side_buttons_ = v;
  }

  bool rotate_display() const {
    return rotate_display_;
  }
  void set_rotate_display(bool v) {
    rotate_display_ = v;
    save_settings_();
  }

  int menu_font_size() const {
    return menu_font_size_;
  }
  void set_menu_font_size(int v) {
    menu_font_size_ = v;
    ListMenuScreen::set_font_size(v);
    save_settings_();
  }

  const std::string& custom_font_path() const {
    return custom_font_path_;
  }
  void set_custom_font_path(const std::string& path) {
    custom_font_path_ = path;
    save_settings_();
  }

  // Empty string = auto-cycle through all images on each sleep.
  const std::string& sleep_image_path() const {
    return sleep_image_path_;
  }
  void set_sleep_image_path(const std::string& path) {
    sleep_image_path_ = path;
    save_settings_();
  }

  const std::string& installed_font_path() const {
    return installed_font_path_;
  }
  void set_installed_font_path(const std::string& path) {
    installed_font_path_ = path;
    save_settings_();
  }

  BookSortOrder sort_order() const {
    return menu_.sort_order();
  }
  void set_sort_order(BookSortOrder order) {
    menu_.set_sort_order(order);
    save_settings_();
  }

  // Called by MainMenu when the user opens a book: updates the open-order
  // counter in the index and persists both the index and settings.
  void record_book_opened(const std::string& path);

  // Navigate to a screen: push on top of the current screen (current stays on stack).
  // Or replace the current screen (pop it first, then push the new one).
  // safe to call from within a screen's update(); the transition happens after update() returns.
  void push_screen(ScreenId id) {
    pending_push_ = id;
    pending_replace_ = ScreenId::None;
    pending_pop_count_ = 0;
  }
  void replace_screen(ScreenId id) {
    pending_push_ = ScreenId::None;
    pending_replace_ = id;
    pending_pop_count_ = 0;
  }
  void pop_screen(int count = 1) {
    pending_push_ = ScreenId::None;
    pending_replace_ = ScreenId::None;
    pending_pop_count_ = count;
  }

  bool has_pending_transition() const {
    return pending_push_ != ScreenId::None || pending_replace_ != ScreenId::None || pending_pop_count_ > 0;
  }

  void start(DrawBuffer& buf, IRuntime& runtime);
  // Auto-open a book by path (skips menu, for debugging).
  void auto_open_book(const char* epub_path, DrawBuffer& buf, IRuntime& runtime);
  void update(const ButtonState& buttons, uint32_t dt_ms, DrawBuffer& buf, IRuntime& runtime);
  bool running() const;
  uint64_t tick_count() const;
  uint32_t uptime_ms() const;

 private:
  ButtonState buttons_{};
  uint64_t ticks_ = 0;
  uint32_t uptime_ms_ = 0;

  bool started_ = false;
  bool running_ = true;

  static constexpr uint32_t kSleepTimeoutMs = 10u * 60u * 1000u;  // 10 minutes
  uint32_t inactivity_ms_ = 0;

  bool invert_menu_buttons_ = false;
  bool invert_bottom_paging_ = true;
  bool invert_side_buttons_ = false;
  bool rotate_display_ = false;

  int menu_font_size_ = 0;
  uint16_t open_counter_ = 0;  // monotonically increasing; incremented each time a book is opened

  std::string custom_font_path_;
  std::string installed_font_path_;
  std::string sleep_image_path_;  // empty = auto-cycle
  int sleep_image_idx_ = 0;

  ScreenManager screen_mgr_;

  MainMenu menu_;
  ReaderScreen reader_;
  SettingsScreen settings_;
  ReaderOptionsScreen reader_options_;
  ChapterSelectScreen chapter_select_;
  LinksScreen links_screen_;
  DeleteConfirmScreen delete_confirm_;

#ifdef MICROREADER_ENABLE_DEMOS
  BouncingBallDemo bouncing_ball_;
  GrayscaleDemo grayscale_demo_;
#endif

  ScreenId pending_push_ = ScreenId::None;
  ScreenId pending_replace_ = ScreenId::None;

  int pending_pop_count_ = 0;

  const BitmapFontSet* reader_font_ = nullptr;
  FontManager* font_manager_ = nullptr;
  std::function<void()> invalidate_font_fn_;

  IScreen* screen_for_(ScreenId id);
};

}  // namespace microreader
