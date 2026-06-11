#include "Application.h"

#include <cstdlib>
#include <cstring>
#include <ctime>

#include "HeapLog.h"
#include "content/BookIndex.h"
#include "content/BmpSleepConverter.h"

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
  delete_confirm_.set_app(this);

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

  // Apply persisted menu font size to all list screens.
  ListMenuScreen::set_font_size(menu_font_size_);

  // Apply persisted display rotation.
  buf.set_rotation(rotate_display_ ? Rotation::Deg0 : Rotation::Deg90);

  screen_mgr_.push(&menu_, buf, runtime);

  // Auto-open last book if one was active at shutdown — but only if the
  // reader font is valid.  Without a font the reader cannot render text, so
  // fall back to the main menu and let the normal pre-book-open hook install
  // the font on the first book open.
  if (!pending_book_path_.empty()) {
    MR_LOGI("app", "auto-open: '%s'", pending_book_path_.c_str());
    if (reader_font_ && reader_font_->valid()) {
      auto_open_book(pending_book_path_.c_str(), buf, runtime);
    } else {
      MR_LOGI("app", "skipping auto-open (no valid font) — starting from main menu");
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

void Application::do_sleep_(DrawBuffer& buf) {
  // Stop the active screen so it can save state (e.g. reading position).
  if (IScreen* top = screen_mgr_.top())
    top->stop();

  // If a specific image is pinned, always show it. Otherwise auto-cycle.
  MR_LOGI("sleep", "do_sleep_: pinned='%s' idx=%d", sleep_image_path_.c_str(), sleep_image_idx_);
  if (!sleep_image_path_.empty()) {
    save_settings_();
    buf.set_rotation(Rotation::Deg90);
    bool shown = false;
    if (sleep_image_path_.rfind("embedded:", 0) == 0) {
      shown = buf.show_sleep_image_embedded(std::atoi(sleep_image_path_.c_str() + 9));
    } else if (sleep_image_path_.rfind("bmp:", 0) == 0) {
      shown = show_bmp_sleep(sleep_image_path_.c_str() + 4, data_dir_, buf);
    } else {
      shown = buf.show_sleep_image(sleep_image_path_.c_str());
    }
    MR_LOGI("sleep", "show result: %d", (int)shown);
    if (!shown && !buf.show_sleep_image_embedded(0))
      buf.deep_sleep();
    running_ = false;
    return;
  }

  // Auto-cycle: build list. Custom SD images take priority over embedded ones.
  std::vector<std::string> images;
#ifdef ESP_PLATFORM
  DIR* d = opendir("/sdcard/.sleep");
  if (d) {
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
      if (ent->d_name[0] == '.')
        continue;
      const char* ext = std::strrchr(ent->d_name, '.');
      if (!ext) continue;
      if (std::strcmp(ext, ".mgr") == 0) {
        images.push_back(std::string("/sdcard/.sleep/") + ent->d_name);
      } else if (std::strcmp(ext, ".bmp") == 0 && data_dir_) {
        images.push_back(std::string("bmp:/sdcard/.sleep/") + ent->d_name);
      }
    }
    closedir(d);
  }
#else
  try {
    for (const auto& entry : fs::directory_iterator("sd/.sleep")) {
      const auto& p = entry.path();
      if (p.extension() == ".mgr")
        images.push_back(p.string());
      else if (p.extension() == ".bmp" && data_dir_)
        images.push_back("bmp:" + p.string());
    }
  } catch (...) {}
#endif
  if (images.empty()) {
    images.push_back("embedded:0");
    images.push_back("embedded:1");
    images.push_back("embedded:2");
  }

  // Pick current image, then advance index for next sleep.
  int idx = sleep_image_idx_ % static_cast<int>(images.size());
  sleep_image_idx_ = (idx + 1) % static_cast<int>(images.size());

  MR_LOGI("sleep", "auto-cycle: %d images, showing idx=%d path='%s'", (int)images.size(), idx, images[idx].c_str());

  // Save state (includes updated sleep_image_idx_).
  save_settings_();

  // Reset rotation before drawing the sleeping screen.
  buf.set_rotation(Rotation::Deg90);

  const std::string& path = images[idx];
  bool sleep_shown = false;
  if (path.rfind("embedded:", 0) == 0) {
    sleep_shown = buf.show_sleep_image_embedded(std::atoi(path.c_str() + 9));
  } else if (path.rfind("bmp:", 0) == 0) {
    sleep_shown = show_bmp_sleep(path.c_str() + 4, data_dir_, buf);
  } else {
    sleep_shown = buf.show_sleep_image(path.c_str());
  }

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
    case ScreenId::DeleteConfirm:
      return &delete_confirm_;

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

  std::fclose(f);
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
    else if (std::sscanf(line, "list_format=%u", &uval) == 1)
      menu_.set_list_format(uval <= 2 ? static_cast<BookListFormat>(uval) : BookListFormat::TitleAndAuthor);
    else if (std::sscanf(line, "sort_order=%u", &uval) == 1)
      menu_.set_sort_order(uval == 1 ? BookSortOrder::ByLastOpened : BookSortOrder::ByName);
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
    else if (std::sscanf(line, "sleep_image=%511[^\n]", sval) == 1)
      sleep_image_path_ = sval;
    else if (std::sscanf(line, "sleep_image_idx=%u", &uval) == 1)
      sleep_image_idx_ = static_cast<int>(uval);
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
