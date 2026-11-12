#include "SettingsScreen.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "../Application.h"
#include "../BuiltinFonts.h"
#include "../content/BookIndex.h"
#include "../version.h"
#include "SleepScreens.h"

#ifdef ESP_PLATFORM
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "esp_system.h"
#include "miniz.h"
#else
#include <filesystem>
#endif

#include "../resources/spiffs_image_data.h"

namespace microreader {

static std::string get_date_time_label(Application* app) {
  IRuntime* rt = app ? app->stats().clock() : nullptr;
  const int64_t now = rt ? rt->local_time() : 0;
  if (!clock_is_valid(now))
    return "Date & Time: not set";
  const CivilDate d = date_from_local_time(now);
  const int sod = seconds_of_day(now);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "Date: %d %s %d, %02d:%02d", d.day, month_abbrev(d.month), d.year, sod / 3600,
                (sod / 60) % 60);
  return buf;
}

static std::string get_goal_label(bool enabled) {
  return std::string("Daily Goal: ") + (enabled ? "On" : "Off");
}

static std::string get_goal_target_label(int minutes) {
  return "Goal: " + format_duration(static_cast<uint32_t>(minutes) * 60u) + " a day";
}

static std::string get_menu_nav_label(bool inverted) {
  return std::string("Menu Nav: ") + (inverted ? "Right=Down" : "Right=Up");
}

static std::string get_bottom_paging_label(bool inverted) {
  return std::string("Bottom Paging: ") + (inverted ? "Right=Prev" : "Right=Next");
}

static std::string get_side_paging_label(bool inverted) {
  return std::string("Side Paging: ") + (inverted ? "Top=Prev" : "Top=Next");
}

static std::string get_sort_order_label(BookSortOrder order) {
  return std::string("Sort: ") + (order == BookSortOrder::LastOpened ? "Last Opened" : "Alphabetical");
}

static std::string get_list_format_label(BookListFormat fmt) {
  if (fmt == BookListFormat::TitleOnly)
    return "Book List: Title";
  if (fmt == BookListFormat::Filename)
    return "Book List: Filename";
  if (fmt == BookListFormat::TitleAndPercent)
    return "Book List: Title & Percent Read";
  return "Book List: Title & Author";
}

static std::string get_rotate_display_label(bool rotated) {
  return std::string("Display: ") + (rotated ? "Landscape" : "Portrait");
}

static std::string get_menu_font_label(int size) {
  return std::string("Menu Size: ") + (size == 0 ? "Small" : (size == 1 ? "Medium" : "Large"));
}

static std::string get_sleep_image_label(const std::string& path) {
  std::string label = "Sleep Screen: ";
  if (path == sleep_screens::kModeCycle)
    return label + "Cycle images";
  if (path == sleep_screens::kModeRandom)
    return label + "Random image";
  if (path == sleep_screens::kModeCover)
    return label + "Book cover";
  if (path == sleep_screens::kModeStats)
    return label + "Reading stats";
  if (path.rfind("embedded:", 0) == 0) {
    int idx = std::atoi(path.c_str() + 9);
    label += (idx == 0 ? "bird" : (idx == 1 ? "stone" : "bebop"));
  } else {
    const char* p = (path.rfind("bmp:", 0) == 0 || path.rfind("img:", 0) == 0) ? path.c_str() + 4 : path.c_str();
    const char* slash = nullptr;
    for (const char* c = p; *c; ++c)
      if (*c == '/' || *c == '\\')
        slash = c;
    label += (slash ? slash + 1 : p);
  }
  return label;
}

static std::string get_home_label(bool covers) {
  return std::string("Home Screen: ") + (covers ? "Covers" : "Book List");
}

// Number of reader text sizes, and the pixel size of one of them. All the
// built-in fonts come in the same sizes (20 to 32 px, in steps of 2).
static int font_size_count(Application* app) {
  if (app && app->font_manager() && app->font_manager()->valid()) {
    const BitmapFontSet* fonts = app->font_manager()->font_set();
    if (fonts && fonts->num_fonts() > 0)
      return fonts->num_fonts();
  }
  return 7;
}

static std::string get_font_size_label(Application* app, int idx) {
  int px = 20 + 2 * idx;
  if (app && app->font_manager() && app->font_manager()->valid()) {
    const BitmapFontSet* fonts = app->font_manager()->font_set();
    if (fonts && idx < fonts->num_fonts())
      if (const BitmapFont* f = fonts->get_font(idx))
        px = f->nominal_size();
  }
  const int n = font_size_count(app);
  const char* hint = idx == 0 ? "  (smallest)" : idx == n - 1 ? "  (largest)" : "";
  char buf[48];
  std::snprintf(buf, sizeof(buf), "Font Size: %d%s", px, hint);
  return buf;
}

static std::string get_font_label(const std::string& font_path) {
  std::string label = "Font: ";
  if (is_builtin_font(font_path)) {
    label += font_path;
  } else {
    const char* p = font_path.c_str();
    const char* slash = nullptr;
    for (const char* c = p; *c; ++c)
      if (*c == '/' || *c == '\\')
        slash = c;
    label += std::string(slash ? slash + 1 : p);
  }
  return label;
}

void SettingsScreen::on_start() {
  title_ = "Settings";
#ifdef MICROREADER_BUILD_NAME
  // "Build 10 (2.0-dev.390)": the release name from release.txt, then the version.
  subtitle_ = sizeof(MICROREADER_BUILD_NAME) > 1 ? MICROREADER_BUILD_NAME " (" MICROREADER_VERSION ")" : MICROREADER_VERSION;
#else
  subtitle_ = MICROREADER_VERSION;
#endif

  sd_fonts_.clear();
  for (const char* f : kBuiltinFonts)
    sd_fonts_.push_back(f);
  font_sel_idx_ = 0;
#ifdef ESP_PLATFORM
  DIR* d = opendir("/sdcard/fonts");
  if (d) {
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
      if (ent->d_name[0] == '.')
        continue;
      const char* ext = std::strrchr(ent->d_name, '.');
      if (ext && strcmp(ext, ".mfb") == 0) {
        sd_fonts_.push_back(std::string("/sdcard/fonts/") + ent->d_name);
      }
    }
    closedir(d);
  }
#else
  namespace fs = std::filesystem;
  try {
    for (const auto& entry : fs::directory_iterator("sd/fonts")) {
      std::string ext = entry.path().extension().string();
      if (ext == ".mfb") {
        sd_fonts_.push_back(entry.path().string());
      }
    }
  } catch (...) {}
#endif

  if (app_) {
    const std::string& current = app_->custom_font_path();
    for (size_t i = 0; i < sd_fonts_.size(); ++i) {
      if (sd_fonts_[i] == current) {
        font_sel_idx_ = static_cast<int>(i);
        break;
      }
    }
  }

  // Sleep screen modes. Every image in the sleep folders is covered by
  // "Random image", so individual images aren't listed.
  sleep_images_.clear();
  sleep_images_.push_back(sleep_screens::kModeRandom);
  sleep_images_.push_back(sleep_screens::kModeCover);
  sleep_images_.push_back(sleep_screens::kModeStats);
  sleep_image_sel_idx_ = 0;
  if (app_) {
    const std::string& current = app_->sleep_image_path();
    for (size_t i = 0; i < sleep_images_.size(); ++i) {
      if (sleep_images_[i] == current) {
        sleep_image_sel_idx_ = static_cast<int>(i);
        break;
      }
    }
  }

  // --- Wi-Fi Transfer ---
  idx_wifi_ = count();
  add_item("Wi-Fi Transfer");
  add_separator();

  // --- Reading statistics ---
  // (Finished Books is the first row of the book list.)
  idx_stats_ = count();
  add_item("Reading Stats");

  idx_goal_ = count();
  add_item(get_goal_label(app_ && app_->goal_enabled()));

  idx_goal_target_ = -1;
  if (app_ && app_->goal_enabled()) {
    idx_goal_target_ = count();
    add_item(get_goal_target_label(app_->goal_minutes()), 1);
  }

  idx_date_time_ = count();
  add_item(get_date_time_label(app_));

  add_separator();

  // --- Appearance ---
  idx_home_ = count();
  add_item(get_home_label(app_ && app_->home_covers()));

  idx_rotate_display_ = count();
  add_item(get_rotate_display_label(app_ && app_->rotate_display()));

  idx_menu_font_ = count();
  add_item(get_menu_font_label(app_ ? app_->menu_font_size() : 0));

  idx_list_format_ = count();
  if (app_) {
    add_item(get_list_format_label(app_->main_menu() ? app_->main_menu()->list_format() : BookListFormat::TitleOnly));
  } else {
    add_item("List: Title");
  }

  idx_sort_order_ = count();
  add_item(get_sort_order_label(app_ ? app_->sort_order() : BookSortOrder::Alphabetical));

  idx_font_ = count();
  add_item(get_font_label(sd_fonts_[font_sel_idx_]));

  idx_font_size_ = count();
  add_item(get_font_size_label(app_, app_ ? app_->reader()->reader_settings().font_size_idx : 1));

  idx_sleep_image_ = count();
  add_item(get_sleep_image_label(sleep_images_[sleep_image_sel_idx_]));

  add_separator();

  // --- Controls ---
  idx_invert_side_ = count();
  add_item(get_side_paging_label(app_->invert_side_buttons()));

  idx_invert_bottom_paging_ = count();
  add_item(get_bottom_paging_label(app_->invert_bottom_paging()));

  idx_invert_menu_ = count();
  add_item(get_menu_nav_label(app_->invert_menu_buttons()));

  add_separator();

  if (data_dir_) {
    idx_clear_cache_ = count();
    add_item("Clear Cache");

    idx_rebuild_index_ = count();
    add_item("Rebuild Book Index");
  }

// Developer tools for the X4's font space and firmware slots. Not on the X3:
// its built-in fonts don't use the font space (which may hold the original
// firmware's data there), and booting the other firmware slot from here
// isn't safe on its layout — the web flasher is the way to switch.
#if defined(ESP_PLATFORM) && !defined(MICROREADER_X3)
  if (app_ && app_->has_invalidate_font_fn()) {
    idx_invalidate_font_ = count();
    add_item("Invalidate Font");
  }

  idx_spiffs_ = count();
  add_item("Rebuild SPIFFS");

  {
    auto running = esp_ota_get_running_partition();
    auto next = esp_ota_get_next_update_partition(running);
    if (next) {
      esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
      esp_ota_get_state_partition(next, &state);
      ESP_LOGI("OTA", "on_start: next=%s state=%d", next->label, (int)state);
      if (state == ESP_OTA_IMG_VALID || state == ESP_OTA_IMG_NEW || state == ESP_OTA_IMG_PENDING_VERIFY ||
          state == ESP_OTA_IMG_UNDEFINED) {
        idx_switch_ota_ = count();
        char label[24];
        std::snprintf(label, sizeof(label), "Switch to %s", next->label);
        add_item(label);
      }
    }
  }
#endif

  // --- Demos ---
#ifdef MICROREADER_ENABLE_DEMOS
  add_separator();
  idx_bouncing_ball_ = count();
  add_item("Bouncing Ball");

  idx_grayscale_demo_ = count();
  add_item("Grayscale Demo");
#endif
}

void SettingsScreen::on_select(int index) {
#ifdef MICROREADER_ENABLE_DEMOS
  if (index == idx_bouncing_ball_) {
    app_->push_screen(ScreenId::BouncingBall);
    return;
  }
  if (index == idx_grayscale_demo_) {
    app_->push_screen(ScreenId::GrayscaleDemo);
    return;
  }
#endif
  if (index == idx_wifi_) {
    app_->push_screen(ScreenId::Wifi);
    return;
  }
  if (index == idx_stats_) {
    app_->push_screen(ScreenId::Stats);
    return;
  }
  if (index == idx_date_time_) {
    app_->push_screen(ScreenId::DateTime);
    return;
  }
  if (index == idx_goal_) {
    app_->set_goal_enabled(!app_->goal_enabled());
    restart();  // adds/removes the "Goal: ..." line under this one
    return;
  }
  if (index == idx_goal_target_) {
    // Step to the next preset; a custom value from the settings file restarts at the first.
    int next = kGoalPresetMinutes[0];
    for (int i = 0; i < kNumGoalPresets; ++i) {
      if (kGoalPresetMinutes[i] == app_->goal_minutes()) {
        next = kGoalPresetMinutes[(i + 1) % kNumGoalPresets];
        break;
      }
    }
    app_->set_goal_minutes(next);
    set_item_label(idx_goal_target_, get_goal_target_label(next));
    return;
  }
  if (index == idx_clear_cache_) {
    clear_cache_();
    toast_original_label_ = get_item_label(idx_clear_cache_);
    toast_idx_ = idx_clear_cache_;
    toast_frames_ = 15;
    set_item_label(idx_clear_cache_, "Cache cleared!");
    return;
  }
  if (index == idx_rebuild_index_) {
    if (app_->main_menu() && app_->main_menu()->has_books_dir() && app_->data_dir_) {
      std::string root_dir = app_->main_menu()->books_dir();
      std::string index_path = std::string(app_->data_dir_) + "/book_index.dat";

      buf_->sync_bw_ram();
      BookIndex::instance().load(index_path);
      BookIndex::instance().build_index(root_dir, *buf_);
      BookIndex::instance().save(index_path);
      buf_->reset_after_scratch(true);
      app_->pop_screen();  // go back to main menu
    }
    return;
  }
  if (index == idx_sort_order_) {
    if (app_) {
      BookSortOrder order = (app_->sort_order() == BookSortOrder::Alphabetical) ? BookSortOrder::LastOpened : BookSortOrder::Alphabetical;
      app_->set_sort_order(order);
      set_item_label(idx_sort_order_, get_sort_order_label(order));
    }
    return;
  }
  if (index == idx_list_format_) {
    if (app_->main_menu()) {
      auto fmt = app_->main_menu()->list_format();
      // Title → Title & Percent Read → Filename → Title & Author → Title
      if (fmt == BookListFormat::TitleAndAuthor) {
        fmt = BookListFormat::TitleOnly;
      } else if (fmt == BookListFormat::TitleOnly) {
        fmt = BookListFormat::TitleAndPercent;
      } else if (fmt == BookListFormat::TitleAndPercent) {
        fmt = BookListFormat::Filename;
      } else {
        fmt = BookListFormat::TitleAndAuthor;
      }
      app_->main_menu()->set_list_format(fmt);
      set_item_label(idx_list_format_, get_list_format_label(fmt));
    }
    app_->save_settings_();
    return;
  }
  if (index == idx_invert_menu_) {
    if (app_) {
      bool v = !app_->invert_menu_buttons();
      app_->set_invert_menu_buttons(v);
      set_item_label(idx_invert_menu_, get_menu_nav_label(v));
    }
    return;
  }
  if (index == idx_invert_bottom_paging_) {
    if (app_) {
      bool v = !app_->invert_bottom_paging();
      app_->set_invert_bottom_paging(v);
      set_item_label(idx_invert_bottom_paging_, get_bottom_paging_label(v));
    }
    return;
  }
  if (index == idx_invert_side_) {
    if (app_) {
      bool v = !app_->invert_side_buttons();
      app_->set_invert_side_buttons(v);
      set_item_label(idx_invert_side_, get_side_paging_label(v));
    }
    return;
  }
  if (index == idx_rotate_display_) {
    if (app_ && buf_) {
      bool v = !app_->rotate_display();
      app_->set_rotate_display(v);
      set_item_label(idx_rotate_display_, get_rotate_display_label(v));
      buf_->set_rotation(v ? Rotation::Deg0 : Rotation::Deg90);
    }
    return;
  }
  if (index == idx_menu_font_) {
    if (app_) {
      int v = (app_->menu_font_size() + 1) % 3;
      app_->set_menu_font_size(v);
      restart();  // rebuilds items with the new font size immediately
    }
    return;
  }
  if (index == idx_home_) {
    if (app_) {
      const bool v = !app_->home_covers();
      app_->set_home_covers(v);
      set_item_label(idx_home_, get_home_label(v));
    }
    return;
  }
  if (index == idx_font_size_) {
    if (app_) {
      // Same setting as Font Size in the book menu (▶ while reading).
      ReaderSettings& rs = app_->reader()->reader_settings();
      const int n = font_size_count(app_);
      rs.font_size_idx = static_cast<uint8_t>((rs.font_size_idx + 1) % n);
      app_->save_settings_();
      set_item_label(idx_font_size_, get_font_size_label(app_, rs.font_size_idx));
    }
    return;
  }
  if (index == idx_font_) {
    if (app_ && !sd_fonts_.empty()) {
      font_sel_idx_ = (font_sel_idx_ + 1) % sd_fonts_.size();
      app_->set_custom_font_path(sd_fonts_[font_sel_idx_]);
      set_item_label(idx_font_, get_font_label(sd_fonts_[font_sel_idx_]));
    }
    return;
  }
  if (index == idx_sleep_image_) {
    if (app_ && !sleep_images_.empty()) {
      sleep_image_sel_idx_ = (sleep_image_sel_idx_ + 1) % static_cast<int>(sleep_images_.size());
      app_->set_sleep_image_path(sleep_images_[sleep_image_sel_idx_]);
      set_item_label(idx_sleep_image_, get_sleep_image_label(sleep_images_[sleep_image_sel_idx_]));
    }
    return;
  }
#ifdef ESP_PLATFORM
  if (index == idx_switch_ota_) {
    switch_ota_partition_();
    return;
  }
  if (index == idx_invalidate_font_) {
    if (app_) {
      app_->set_installed_font_path("");
      app_->invalidate_font();
      toast_original_label_ = get_item_label(idx_invalidate_font_);
      toast_idx_ = idx_invalidate_font_;
      toast_frames_ = 15;
      set_item_label(idx_invalidate_font_, "Font invalidated!");
    }
    return;
  }
  if (index == idx_spiffs_) {
    const esp_partition_t* part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");

    if (part) {
      buf_->sync_bw_ram();
      buf_->show_loading("Erasing...", 0);

      static constexpr size_t kDictSize = TINFL_LZ_DICT_SIZE;
      static constexpr size_t kDecompSize = 11264;
      static constexpr size_t kWriteSize = 4096;
      uint8_t* work = static_cast<uint8_t*>(malloc(kDecompSize + kDictSize + kWriteSize));
      if (work) {
        esp_partition_erase_range(part, 0, part->size);

        if (buf_)
          buf_->show_loading("Writing...", 50);

        auto* decomp = reinterpret_cast<tinfl_decompressor*>(work);
        uint8_t* dict = work + kDecompSize;
        uint8_t* wbuf = work + kDecompSize + kDictSize;
        tinfl_init(decomp);
        const uint8_t* in_ptr = kSpiffsImage;
        size_t in_left = kSpiffsImageSize;
        size_t flash_offset = 0;
        size_t dict_ofs = 0;
        tinfl_status status = TINFL_STATUS_HAS_MORE_OUTPUT;
        while (status == TINFL_STATUS_HAS_MORE_OUTPUT || status == TINFL_STATUS_NEEDS_MORE_INPUT) {
          size_t in_sz = in_left;
          size_t out_sz = kDictSize - dict_ofs;
          mz_uint32 flags = TINFL_FLAG_PARSE_ZLIB_HEADER;
          if (in_left > in_sz)
            flags |= TINFL_FLAG_HAS_MORE_INPUT;
          status = tinfl_decompress(decomp, in_ptr, &in_sz, dict, dict + dict_ofs, &out_sz, flags);
          in_ptr += in_sz;
          in_left -= in_sz;
          size_t produced = out_sz;
          size_t write_ofs = 0;
          while (write_ofs < produced) {
            size_t chunk = produced - write_ofs;
            if (chunk > kWriteSize)
              chunk = kWriteSize;
            memcpy(wbuf, dict + dict_ofs + write_ofs, chunk);
            esp_partition_write(part, flash_offset, wbuf, chunk);
            flash_offset += chunk;
            write_ofs += chunk;
          }
          dict_ofs = (dict_ofs + produced) & (kDictSize - 1);
          if (status <= TINFL_STATUS_DONE)
            break;
        }
        free(work);

        buf_->show_loading("Done!", 100);
      }
    }
    esp_restart();
  }
#endif
  return;
}

#ifdef ESP_PLATFORM
// Fallback: write the otadata partition directly, bypassing esp_image_verify.
// Mirrors what tools/switch_partition.py does from the host side.
// Use only when esp_ota_set_boot_partition refuses an image we know is
// good (e.g. foreign firmware whose seg0 is too large to mmap from a
// running app on ESP32-C3).
static esp_err_t force_switch_via_otadata_(const esp_partition_t* next) {
  if (!next)
    return ESP_ERR_INVALID_ARG;
  const esp_partition_t* otadata =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (!otadata) {
    ESP_LOGE("OTA", "otadata partition not found");
    return ESP_ERR_NOT_FOUND;
  }

  // ota_seq: 1 -> ota_0 (app0), 2 -> ota_1 (app1)
  uint32_t seq = (next->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) ? 1u : 2u;
  uint8_t entry[32];
  std::memset(entry, 0xFF, sizeof(entry));
  std::memcpy(entry, &seq, 4);
  uint32_t crc = esp_rom_crc32_le(UINT32_MAX, reinterpret_cast<const uint8_t*>(&seq), 4);
  std::memcpy(entry + 28, &crc, 4);

  esp_err_t err = esp_partition_erase_range(otadata, 0, otadata->size);
  if (err != ESP_OK) {
    ESP_LOGE("OTA", "otadata erase failed: %s", esp_err_to_name(err));
    return err;
  }
  err = esp_partition_write(otadata, 0, entry, sizeof(entry));
  if (err != ESP_OK) {
    ESP_LOGE("OTA", "otadata write failed: %s", esp_err_to_name(err));
    return err;
  }
  ESP_LOGW("OTA", "Forced boot slot via otadata write (seq=%u -> %s)", (unsigned)seq, next->label);
  return ESP_OK;
}

void SettingsScreen::switch_ota_partition_() {
  auto running = esp_ota_get_running_partition();
  auto next = esp_ota_get_next_update_partition(running);
  ESP_LOGI("OTA", "Running: %s @ 0x%08lx", running ? running->label : "null",
           running ? (unsigned long)running->address : 0UL);
  ESP_LOGI("OTA", "Next:    %s @ 0x%08lx", next ? next->label : "null", next ? (unsigned long)next->address : 0UL);
  if (!next) {
    ESP_LOGE("OTA", "No next OTA partition found, aborting");
    return;
  }

  esp_err_t ret = esp_ota_set_boot_partition(next);
  if (ret == ESP_OK) {
    ESP_LOGI("OTA", "Switching to %s, restarting", next->label);
    esp_restart();
    return;
  }

  ESP_LOGW("OTA", "esp_ota_set_boot_partition refused (%s); falling back to direct otadata write",
           esp_err_to_name(ret));
  if (force_switch_via_otadata_(next) == ESP_OK) {
    ESP_LOGI("OTA", "Restarting into %s (unverified)", next->label);
    esp_restart();
  } else {
    ESP_LOGE("OTA", "Forced switch failed; staying on %s", running ? running->label : "running");
  }
}
#endif

void SettingsScreen::clear_cache_() {
  if (!data_dir_)
    return;
#ifdef ESP_PLATFORM
  char cache_dir[768];
  std::snprintf(cache_dir, sizeof(cache_dir), "%s/cache", data_dir_);
  DIR* d = opendir(cache_dir);
  if (!d) {
    mkdir(cache_dir, 0775);
    return;
  }
  struct dirent* ent;
  char subdir_path[768];
  while ((ent = readdir(d)) != nullptr) {
    if (ent->d_name[0] == '.')
      continue;
    std::snprintf(subdir_path, sizeof(subdir_path), "%s/%s", cache_dir, ent->d_name);
    // Remove all files inside the per-book subdir.
    DIR* sd = opendir(subdir_path);
    if (sd) {
      struct dirent* sf;
      char file_path[768];
      while ((sf = readdir(sd)) != nullptr) {
        if (sf->d_name[0] == '.')
          continue;
        std::snprintf(file_path, sizeof(file_path), "%s/%s", subdir_path, sf->d_name);
        std::remove(file_path);
      }
      closedir(sd);
    }
    rmdir(subdir_path);
  }
  closedir(d);
  rmdir(cache_dir);
  mkdir(cache_dir, 0775);
#else
  namespace fs = std::filesystem;
  try {
    std::string cache_path = std::string(data_dir_) + "/cache";
    fs::remove_all(cache_path);
    fs::create_directories(cache_path);
  } catch (...) {}
#endif
}

}  // namespace microreader
