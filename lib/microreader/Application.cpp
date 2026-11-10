#include "Application.h"

#include <cstdlib>
#include <cstring>
#include <ctime>

#include "HeapLog.h"
#include "content/BookIndex.h"
#include "content/BmpSleepConverter.h"
#include "screens/SleepScreens.h"

#ifdef ESP_PLATFORM
#include <dirent.h>
#include <sys/stat.h>

#include "esp_random.h"
#else
#include <filesystem>
#endif

#ifndef ESP_PLATFORM
namespace fs = std::filesystem;
#endif

namespace microreader {

void Application::start(DrawBuffer& buf, IRuntime& runtime) {
  ticks_ = 0;
  uptime_ms_ = 0;
  buttons_ = ButtonState{};
  started_ = true;
  running_ = true;

  // Reading statistics take their time from the platform clocks.
  stats_.set_clock(&runtime);

#ifdef ESP_PLATFORM
  std::srand(esp_random());
#else
  std::srand(static_cast<unsigned>(std::time(nullptr)));
#endif

  if (reader_font_)
    reader_.set_fonts(reader_font_);

  menu_.set_app(this);
  reader_.set_app(this);
  settings_.set_app(this);
  reader_options_.set_app(this);
  chapter_select_.set_app(this);
  links_screen_.set_app(this);
  stats_screen_.set_app(this);
  date_time_.set_app(this);
  finished_screen_.set_app(this);
  wifi_menu_.set_app(this);
  text_input_.set_app(this);
  file_transfer_.set_app(this);

#ifdef MICROREADER_ENABLE_DEMOS
  bouncing_ball_.set_app(this);
  grayscale_demo_.set_app(this);
#endif

  // Set up settings file path if data_dir_ is set
  if (data_dir_)
    settings_path_ = std::string(data_dir_) + "/settings";

  // Load settings first so initial_selection_ and reader settings are ready
  // before the menu's on_start() (directory scan + selection restore) runs.
  load_settings_();
  finished_books_.load();
  book_progress_.load();
  saved_wifi_.load();

  // After a cold power-on the wall clock restarts at 1970; continue from the
  // last saved time so reading stats keep a plausible date.
  restore_clock_(runtime);

  // Apply persisted menu font size to all list screens.
  ListMenuScreen::set_font_size(menu_font_size_);

  // Apply persisted display rotation.
  buf.set_rotation(rotate_display_ ? Rotation::Deg0 : Rotation::Deg90);

  screen_mgr_.push(&menu_, buf, runtime);

  // Auto-open last book if one was active at shutdown — but only if the font
  // is valid. cache_only=true tells the reader not to convert if the MRB is
  // missing; it will pop back to the book list instead of blocking the UI.
  if (!pending_book_path_.empty()) {
    MR_LOGI("app", "auto-open: '%s'", pending_book_path_.c_str());
    if (reader_font_ && reader_font_->valid()) {
      reader_.set_cache_only(true);
      auto_open_book(pending_book_path_.c_str(), buf, runtime);
    } else {
      MR_LOGI("app", "skipping auto-open (no valid font) — starting from book list");
    }
    pending_book_path_.clear();
  }

  // Restore settings screen if it was active
  if (pending_screen_ == "settings") {
    screen_mgr_.push(&settings_, buf, runtime);
  }
  pending_screen_.clear();

  buf.full_refresh();
}

void Application::auto_open_book(const char* epub_path, DrawBuffer& buf, IRuntime& runtime) {
  reader_.set_path(epub_path);
  if (reader_font_)
    reader_.set_fonts(reader_font_);

  screen_mgr_.push(&reader_, buf, runtime);
}

// Convert/cache a BMP sleep image and display it. Returns true if shown.
static bool show_bmp_sleep(const char* bmp_path, const char* data_dir, DrawBuffer& buf) {
  if (!data_dir) return false;
  const char* slash = std::strrchr(bmp_path, '/');
  const char* back  = std::strrchr(bmp_path, '\\');
  if (back > slash) slash = back;
  const char* bname = slash ? slash + 1 : bmp_path;
  const char* dot   = std::strrchr(bname, '.');
  int nlen = dot ? (int)(dot - bname) : (int)std::strlen(bname);
  char cache_dir[256];
  std::snprintf(cache_dir, sizeof(cache_dir), "%s/cache/sleep", data_dir);
  char cache_path[384];
  std::snprintf(cache_path, sizeof(cache_path), "%s/%.*s.mgr", cache_dir, nlen, bname);
  bool cached = false;
  { std::FILE* cf = std::fopen(cache_path, "rb"); if (cf) { std::fclose(cf); cached = true; } }
  if (!cached) {
#ifdef ESP_PLATFORM
    char parent[256];
    std::snprintf(parent, sizeof(parent), "%s/cache", data_dir);
    mkdir(parent, 0775);
    mkdir(cache_dir, 0775);
#else
    try { fs::create_directories(cache_dir); } catch (...) {}
#endif
    MR_LOGI("sleep", "converting BMP: %s", bmp_path);
    cached = convert_bmp_to_mgr2(bmp_path, cache_path);
    MR_LOGI("sleep", "BMP convert result: %d cache=%s", (int)cached, cache_path);
  }
  return cached && buf.show_sleep_image(cache_path);
}

// Show one sleep image entry (see SleepScreens.h for the formats). The .mgr /
// BMP / embedded paths refresh the panel themselves; JPEG/PNG images are
// drawn into the buffer and finished here with a full black & white refresh.
bool Application::show_sleep_entry_(const std::string& path, DrawBuffer& buf) {
  if (path.rfind("embedded:", 0) == 0)
    return buf.show_sleep_image_embedded(std::atoi(path.c_str() + 9));
  if (path.rfind("bmp:", 0) == 0)
    return show_bmp_sleep(path.c_str() + 4, data_dir_, buf);
  if (path.rfind("img:", 0) == 0) {
    if (!sleep_screens::draw_image_file(buf, path.substr(4), data_dir_))
      return false;
    sleep_screens::draw_sleeping_label(buf);
    finish_bw_sleep_(buf);
    return true;
  }
  return buf.show_sleep_image(path.c_str());
}

void Application::finish_bw_sleep_(DrawBuffer& buf) {
  buf.full_refresh(RefreshMode::Full, /*turnOffScreen=*/true);
  buf.deep_sleep();
}

void Application::do_sleep_(DrawBuffer& buf) {
  // Stop the active screen so it can save state (e.g. reading position).
  if (IScreen* top = screen_mgr_.top())
    top->stop();
  // Never leave the radio on while asleep (e.g. sleeping from the Wi-Fi menu).
  if (wifi_)
    wifi_->stop();
  // Write out reading time not yet saved (e.g. reader paused under its options menu).
  stats_.flush();

  const std::string mode = sleep_image_path_;
  MR_LOGI("sleep", "do_sleep_: mode='%s' idx=%d", mode.c_str(), sleep_image_idx_);

  // Sleep screens are always drawn in portrait.
  buf.set_rotation(Rotation::Deg90);

  // Book cover with reading progress.
  if (mode == sleep_screens::kModeCover) {
    save_settings_();
    if (!last_book_path_.empty() &&
        sleep_screens::draw_book_cover(buf, last_book_path_, last_book_title_, last_book_pct_, data_dir_)) {
      finish_bw_sleep_(buf);
      running_ = false;
      return;
    }
    MR_LOGI("sleep", "no book cover available; showing an image instead");
  }

  // Reading Stats calendar with today selected.
  if (mode == sleep_screens::kModeStats) {
    save_settings_();
    if (IRuntime* rt = stats_.clock()) {
      stats_screen_.render_for_sleep(buf, *rt);
      finish_bw_sleep_(buf);
      running_ = false;
      return;
    }
  }

  // A single pinned image.
  const bool pinned = !mode.empty() && mode != sleep_screens::kModeRandom && mode != sleep_screens::kModeCover &&
                      mode != sleep_screens::kModeStats;
  if (pinned) {
    save_settings_();
    const bool shown = show_sleep_entry_(mode, buf);
    MR_LOGI("sleep", "show result: %d", (int)shown);
    if (!shown && !buf.show_sleep_image_embedded(0))
      buf.deep_sleep();
    running_ = false;
    return;
  }

  // Cycle (or pick at random) through the SD card images. Custom images take
  // priority; the built-in ones are used only when there are none.
  std::vector<std::string> images = sleep_screens::list_sd_images(data_dir_ != nullptr);
  if (images.empty()) {
    images.push_back("embedded:0");
    images.push_back("embedded:1");
    images.push_back("embedded:2");
  }
  const int n = static_cast<int>(images.size());
  int idx;
  if (mode == sleep_screens::kModeRandom) {
    idx = std::rand() % n;
    if (n > 1 && idx == sleep_image_idx_ % n)
      idx = (idx + 1 + std::rand() % (n - 1)) % n;  // never the same image twice in a row
    sleep_image_idx_ = idx;
  } else {
    idx = sleep_image_idx_ % n;
    sleep_image_idx_ = (idx + 1) % n;
  }

  MR_LOGI("sleep", "%s: %d images, showing idx=%d path='%s'", mode.empty() ? "cycle" : mode.c_str(), n, idx,
          images[idx].c_str());

  // Save state (includes the updated sleep_image_idx_).
  save_settings_();

  const bool sleep_shown = show_sleep_entry_(images[idx], buf);
  MR_LOGI("sleep", "show result: %d", (int)sleep_shown);
  if (!sleep_shown && !buf.show_sleep_image_embedded(0))
    buf.deep_sleep();

  running_ = false;
}

void Application::update(const ButtonState& buttons, uint32_t dt_ms, DrawBuffer& buf, IRuntime& runtime) {
  if (!started_)
    start(buf, runtime);
  if (!running_)
    return;

  ++ticks_;
  uptime_ms_ += dt_ms;
  buttons_ = buttons;

  // Inactivity / auto-sleep tracking
  if (buttons_.current != 0 || buttons_.pressed_latch != 0) {
    inactivity_ms_ = 0;
  } else {
    inactivity_ms_ += dt_ms;
    if (inactivity_ms_ >= kSleepTimeoutMs) {
      MR_LOGI("app", "auto-sleep after %u ms idle", inactivity_ms_);
      do_sleep_(buf);
      return;
    }
  }

  if (buttons_.is_pressed(Button::Power)) {
    do_sleep_(buf);
    return;
  }

  IScreen* top = screen_mgr_.top();
  if (top) {
    top->update(buttons_, buf, runtime);

    // Process pending navigation (queued by screens via push_screen/replace_screen).
    if (pending_replace_ != ScreenId::None) {
      ScreenId id = pending_replace_;
      pending_replace_ = ScreenId::None;
      screen_mgr_.pop(buf, runtime);
      screen_mgr_.push(screen_for_(id), buf, runtime);
      buf.refresh();
    } else if (pending_push_ != ScreenId::None) {
      ScreenId id = pending_push_;
      pending_push_ = ScreenId::None;
      screen_mgr_.push(screen_for_(id), buf, runtime);
      buf.refresh();
    } else if (pending_pop_count_ > 0) {
      int count = pending_pop_count_;
      pending_pop_count_ = 0;
      if (top == &reader_ || top == &reader_options_)
        save_settings_();
      screen_mgr_.pop(count, buf, runtime);
      buf.refresh();
    }
  }
}  // namespace microreader

IScreen* microreader::Application::screen_for_(ScreenId id) {
  switch (id) {
    case ScreenId::MainMenu:
      return &menu_;
    case ScreenId::Reader:
      return &reader_;
    case ScreenId::Settings:
      return &settings_;
    case ScreenId::ReaderOptions:
      return &reader_options_;
    case ScreenId::ChapterSelect:
      return &chapter_select_;
    case ScreenId::Links:
      return &links_screen_;
    case ScreenId::Stats:
      return &stats_screen_;
    case ScreenId::DateTime:
      return &date_time_;
    case ScreenId::Finished:
      return &finished_screen_;
    case ScreenId::Wifi:
      return &wifi_menu_;
    case ScreenId::TextInput:
      return &text_input_;
    case ScreenId::FileTransfer:
      return &file_transfer_;

#ifdef MICROREADER_ENABLE_DEMOS
    case ScreenId::BouncingBall:
      return &bouncing_ball_;
    case ScreenId::GrayscaleDemo:
      return &grayscale_demo_;
#endif

    default:
      return nullptr;
  }
}
void microreader::Application::save_settings_() {
  if (settings_path_.empty())
    return;
  FILE* f = std::fopen(settings_path_.c_str(), "w");
  if (!f)
    return;

  // Version tag
  std::fprintf(f, "v=1\n");

  // Last screen / book — treat reader-is-anywhere-in-stack as "reader" so
  // shutting down from ReaderOptionsScreen still boots back into the reader.
  ReaderScreen* reader = &reader_;
  const bool settings_active = screen_mgr_.contains(&settings_);
  const bool reader_active = screen_mgr_.contains(reader);

  if (settings_active) {
    std::fprintf(f, "screen=settings\n");
    std::fprintf(f, "setting_sel=%d\n", settings_.selected_index());
  } else if (reader_active) {
    std::fprintf(f, "screen=reader\n");
  } else {
    std::fprintf(f, "screen=menu\n");
  }

  if (reader_active && reader->has_path())
    std::fprintf(f, "book_path=%s\n", reader->get_path().c_str());

  // Last book-list selection: prefer the currently highlighted entry so
  // power-off while browsing still saves position; fall back to last opened.
  const std::string& sel =
      !menu_.current_book_path().empty() ? menu_.current_book_path() : menu_.last_selected_book_path();
  if (!sel.empty())
    std::fprintf(f, "book_sel=%s\n", sel.c_str());

  // Reader display settings
  const ReaderSettings& rs = reader->reader_settings();
  std::fprintf(f, "align_override=%u\n", static_cast<unsigned>(rs.align_override));
  std::fprintf(f, "padding_h=%u\n", static_cast<unsigned>(rs.padding_h_idx));
  std::fprintf(f, "padding_v=%u\n", static_cast<unsigned>(rs.padding_v_idx));
  std::fprintf(f, "spacing_override=%u\n", static_cast<unsigned>(rs.spacing_override));
  std::fprintf(f, "progress=%u\n", static_cast<unsigned>(rs.progress_style));
  std::fprintf(f, "progress_scope=%u\n", static_cast<unsigned>(rs.progress_scope));
  std::fprintf(f, "override_pub_fonts=%u\n", rs.override_publisher_fonts ? 1u : 0u);
  std::fprintf(f, "font_size=%u\n", static_cast<unsigned>(rs.font_size_idx));
  std::fprintf(f, "chapter_bar=%u\n", rs.chapter_bar ? 1u : 0u);

  // Menu list format
  std::fprintf(f, "list_format=%u\n", static_cast<unsigned>(menu_.list_format()));
  std::fprintf(f, "sort_order=%u\n", static_cast<unsigned>(menu_.sort_order()));
  std::fprintf(f, "open_counter=%u\n", static_cast<unsigned>(open_counter_));
  std::fprintf(f, "inv_menu=%u\n", invert_menu_buttons_ ? 1u : 0u);
  std::fprintf(f, "inv_bpage=%u\n", invert_bottom_paging_ ? 1u : 0u);
  std::fprintf(f, "inv_side=%u\n", invert_side_buttons_ ? 1u : 0u);
  std::fprintf(f, "rotate_display=%u\n", rotate_display_ ? 1u : 0u);
  std::fprintf(f, "menu_font_size=%d\n", menu_font_size_);

  if (!custom_font_path_.empty())
    std::fprintf(f, "custom_font=%s\n", custom_font_path_.c_str());
  if (!installed_font_path_.empty())
    std::fprintf(f, "inst_font=%s\n", installed_font_path_.c_str());
  if (!sleep_image_path_.empty())
    std::fprintf(f, "sleep_image=%s\n", sleep_image_path_.c_str());
  std::fprintf(f, "sleep_image_idx=%d\n", sleep_image_idx_);
  if (!last_book_path_.empty()) {
    std::fprintf(f, "last_book=%s\n", last_book_path_.c_str());
    std::fprintf(f, "last_title=%s\n", last_book_title_.c_str());
    std::fprintf(f, "last_pct=%d\n", last_book_pct_);
  }
  std::fprintf(f, "goal_on=%u\n", goal_enabled_ ? 1u : 0u);
  std::fprintf(f, "goal_min=%d\n", goal_minutes_);

  // Per-book percentages (only written when something changed).
  book_progress_.save();

  // Wall clock, so it can be restored after a cold power-on (see restore_clock_).
  if (IRuntime* rt = stats_.clock()) {
    const int64_t now = rt->local_time();
    if (clock_is_valid(now)) {
      saved_clock_ = now;
      std::fprintf(f, "clock=%lu\n", static_cast<unsigned long>(now));
    }
  }

  std::fclose(f);
}

void microreader::Application::restore_clock_(IRuntime& runtime) {
  if (clock_is_valid(runtime.local_time()))
    return;  // clock survived (deep sleep keeps the RTC running) or has no RTC to lose
  int64_t t = firmware_build_time();
  if (saved_clock_ > t)
    t = saved_clock_;
  if (runtime.set_local_time(t))
    MR_LOGI("clock", "restored wall clock to %lu (saved=%lu)", static_cast<unsigned long>(t),
            static_cast<unsigned long>(saved_clock_));
}
void microreader::Application::record_book_opened(const std::string& path) {
  BookIndex::instance().set_last_opened(path, ++open_counter_);
  if (data_dir_) {
    std::string index_path = std::string(data_dir_) + "/book_index.dat";
    BookIndex::instance().save(index_path);
  }
  save_settings_();
}
void microreader::Application::load_settings_() {
  if (settings_path_.empty())
    return;
  FILE* f = std::fopen(settings_path_.c_str(), "r");
  if (!f)
    return;

  char line[512];
  std::string last_screen, last_book_path, book_sel;
  int setting_sel = 0;
  ReaderSettings& rs = reader_.reader_settings();

  while (std::fgets(line, sizeof(line), f)) {
    // Strip trailing newline
    char* nl = std::strchr(line, '\n');
    if (nl)
      *nl = 0;

    char sval[512];
    unsigned uval = 0;
    if (std::sscanf(line, "screen=%511s", sval) == 1)
      last_screen = sval;
    else if (std::sscanf(line, "setting_sel=%d", &setting_sel) == 1)
      ;
    else if (std::sscanf(line, "book_path=%511[^\n]", sval) == 1)
      last_book_path = sval;
    else if (std::sscanf(line, "book_sel=%511[^\n]", sval) == 1)
      book_sel = sval;
    else if (std::sscanf(line, "align_override=%u", &uval) == 1)
      rs.align_override =
          uval < ReaderSettings::kNumAlignPresets ? static_cast<AlignOverride>(uval) : AlignOverride::Book;
    else if (std::sscanf(line, "justify=%u", &uval) == 1)  // Backwards compatibility
      rs.align_override = uval != 0 ? AlignOverride::Justify : AlignOverride::Left;
    else if (std::sscanf(line, "padding_h=%u", &uval) == 1)
      rs.padding_h_idx = uval < ReaderSettings::kNumPresets ? static_cast<uint8_t>(uval) : 1;
    else if (std::sscanf(line, "padding_v=%u", &uval) == 1)
      rs.padding_v_idx = uval < ReaderSettings::kNumPresets ? static_cast<uint8_t>(uval) : 1;
    else if (std::sscanf(line, "spacing_override=%u", &uval) == 1)
      rs.spacing_override = uval < ReaderSettings::kNumSpacingPresets ? static_cast<SpacingOverride>(uval)
                                                                      : SpacingOverride::Spacing_1_0x;
    else if (std::sscanf(line, "line_spacing=%u", &uval) == 1)  // Backwards compatibility
      rs.spacing_override = SpacingOverride::Book;
    else if (std::sscanf(line, "progress=%u", &uval) == 1)
      rs.progress_style = uval <= 2 ? static_cast<ProgressStyle>(uval) : ProgressStyle::Bar;
    else if (std::sscanf(line, "progress_scope=%u", &uval) == 1)
      rs.progress_scope = uval <= 1 ? static_cast<ProgressScope>(uval) : ProgressScope::Book;
    else if (std::sscanf(line, "override_pub_fonts=%u", &uval) == 1)
      rs.override_publisher_fonts = (uval != 0);
    else if (std::sscanf(line, "font_size=%u", &uval) == 1)
      rs.font_size_idx = uval < kMaxFontSizes ? static_cast<uint8_t>(uval) : 1;
    else if (std::sscanf(line, "chapter_bar=%u", &uval) == 1)
      rs.chapter_bar = (uval != 0);
    else if (std::sscanf(line, "list_format=%u", &uval) == 1)
      menu_.set_list_format(uval <= 3 ? static_cast<BookListFormat>(uval) : BookListFormat::TitleAndAuthor);
    else if (std::sscanf(line, "sort_order=%u", &uval) == 1)
      menu_.set_sort_order(uval == 1 ? BookSortOrder::LastOpened : BookSortOrder::Alphabetical);
    else if (std::sscanf(line, "open_counter=%u", &uval) == 1)
      open_counter_ = uval;
    else if (std::sscanf(line, "inv_menu=%u", &uval) == 1)
      invert_menu_buttons_ = (uval != 0);
    else if (std::sscanf(line, "inv_bpage=%u", &uval) == 1)
      invert_bottom_paging_ = (uval != 0);
    else if (std::sscanf(line, "inv_side=%u", &uval) == 1)
      invert_side_buttons_ = (uval != 0);
    else if (std::sscanf(line, "rotate_display=%u", &uval) == 1)
      rotate_display_ = (uval != 0);
    else if (std::sscanf(line, "menu_font_size=%u", &uval) == 1)
      menu_font_size_ = static_cast<int>(uval > 2 ? 2 : uval);
    else if (std::sscanf(line, "custom_font=%511[^\n]", sval) == 1)
      custom_font_path_ = sval;
    else if (std::sscanf(line, "inst_font=%511[^\n]", sval) == 1)
      installed_font_path_ = sval;
    else if (std::sscanf(line, "sleep_image=%511[^\n]", sval) == 1) {
      // Settings only offers random / cover / stats now; anything else (the
      // old "cycle" mode or a single pinned image) becomes random.
      const std::string v = sval;
      sleep_image_path_ = (v == sleep_screens::kModeCover || v == sleep_screens::kModeStats)
                              ? v
                              : std::string(sleep_screens::kModeRandom);
    }
    else if (std::sscanf(line, "sleep_image_idx=%u", &uval) == 1)
      sleep_image_idx_ = static_cast<int>(uval);
    else if (std::sscanf(line, "last_book=%511[^\n]", sval) == 1)
      last_book_path_ = sval;
    else if (std::sscanf(line, "last_title=%511[^\n]", sval) == 1)
      last_book_title_ = sval;
    else if (std::sscanf(line, "last_pct=%u", &uval) == 1)
      last_book_pct_ = uval > 100 ? 100 : static_cast<int>(uval);
    else if (std::sscanf(line, "goal_on=%u", &uval) == 1)
      goal_enabled_ = (uval != 0);
    else if (std::sscanf(line, "goal_min=%u", &uval) == 1)
      goal_minutes_ = (uval >= 1 && uval <= 24 * 60) ? static_cast<int>(uval) : kDefaultGoalMinutes;
    else if (std::strncmp(line, "clock=", 6) == 0)
      saved_clock_ = static_cast<int64_t>(std::strtoul(line + 6, nullptr, 10));
  }
  std::fclose(f);

  MR_LOGI("app", "Loaded settings: align=%u ph=%u pv=%u ls=%u prog=%u sel=%s", static_cast<unsigned>(rs.align_override),
          rs.padding_h_idx, rs.padding_v_idx, static_cast<unsigned>(rs.spacing_override),
          static_cast<unsigned>(rs.progress_style), book_sel.c_str());

  // Restore book list selection highlight
  if (!book_sel.empty())
    menu_.set_initial_selection(book_sel.c_str());

  // Restore settings menu selection
  settings_.set_initial_selection(setting_sel);

  // Store the book to auto-open; actual push happens in start() after buf is ready.
  if (last_screen == "reader" && !last_book_path.empty())
    pending_book_path_ = last_book_path;

  pending_screen_ = last_screen;
}

bool Application::running() const {
  return running_;
}
uint64_t Application::tick_count() const {
  return ticks_;
}
uint32_t Application::uptime_ms() const {
  return uptime_ms_;
}

}  // namespace microreader
