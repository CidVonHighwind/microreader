#include "MainMenu.h"

#include <cstdio>
#include <cstring>

#include "../Application.h"
#include "../HeapLog.h"
#include "../content/BookIndex.h"

#ifdef ESP_PLATFORM
#include <dirent.h>
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

namespace microreader {

void MainMenu::on_start() {
  title_ = "Microreader";

  if (!app_->data_dir_) {
    needs_scan_ = false;
    return;
  }

  std::string index_path = std::string(app_->data_dir_) + "/book_index.dat";

  if (BookIndex::instance().load(index_path)) {
    populate_list_();
    needs_scan_ = false;
  } else {
    // We defer heavy scanning to update() so we don't trip hardware watchdog.
    needs_scan_ = true;
  }
}

void MainMenu::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) {
  if (needs_scan_) {
    needs_scan_ = false;
    scan_directory_(buf);
    populate_list_();

    // Force a redraw and full refresh since the list contents completely changed
    draw_all_(buf, runtime.battery_percentage());
    buf.full_refresh();
  }

  // === DELETE CONFIRMATION MODE ===
  if (delete_pending_) {
    Button btn;
    while (buttons.next_press(btn)) {
      if (btn == Button::Button1) {
        do_delete_(buf, runtime);
        return;
      }
      if (btn == Button::Button0) {
        cancel_delete_(buf, runtime);
        return;
      }
    }
    draw_all_(buf, runtime.battery_percentage());
    buf.refresh();
    return;
  }

  // === NORMAL MODE ===

  // Track Button1 hold duration (time-based for frame-rate independence)
  if (buttons.is_down(Button::Button1))
    button1_hold_ms_ += runtime.frame_time_ms();
  else
    button1_hold_ms_ = 0;

  // Let parent handle up/down/back navigation.
  // on_select() stores the index but no longer opens the book immediately.
  ListMenuScreen::update(buttons, buf, runtime);

  // Handle pending selection (set by on_select above)
  if (pending_select_idx_ >= 0) {
    // Button released → short press → open the book
    if (!buttons.is_down(Button::Button1)) {
      pending_select_idx_ = -1;
      app_->push_screen(ScreenId::Reader);
      return;
    }
    // Long press threshold reached → enter delete confirmation
    if (button1_hold_ms_ >= kLongPressMs) {
      const int idx = pending_select_idx_;
      pending_select_idx_ = -1;
      button1_hold_ms_ = 0;
      delete_pending_ = true;
      delete_index_ = idx;
      delete_book_label_ = entries_[idx].label;
      title_ = "Delete?";
      set_item_label(idx, "Sel=Yes  Back=No");
      draw_all_(buf, runtime.battery_percentage());
      buf.refresh();
      return;
    }
    // Button still held but not yet long enough — wait another frame
  }
}

void MainMenu::on_select(int index) {
  // Don't open the book immediately — update() handles short-press vs long-press
  last_selected_path_ = entries_[index].path;
  app_->reader()->set_path(entries_[index].path.c_str());
  pending_select_idx_ = index;
}

void MainMenu::on_back() {
  app_->push_screen(ScreenId::Settings);
}

void MainMenu::do_delete_(DrawBuffer& buf, IRuntime& runtime) {
  const std::string& epub_path = entries_[delete_index_].path;
  const char* path_cstr = epub_path.c_str();

  MR_LOGI("menu", "deleting %s", path_cstr);

  // 1. Delete the EPUB file
  (void)std::remove(path_cstr);

  // 2. Delete the MRB cache directory (<data_dir>/cache/<basename>/)
  if (app_ && app_->data_dir_) {
    std::string cache_dir = std::string(app_->data_dir_) + "/cache/";
    const char* name = path_cstr;
    const char* sep = std::strrchr(name, '/');
#ifdef _WIN32
    const char* bsep = std::strrchr(name, '\\');
    if (bsep && (!sep || bsep > sep))
      sep = bsep;
#endif
    if (sep)
      name = sep + 1;
    const char* dot = std::strrchr(name, '.');
    if (dot)
      cache_dir.append(name, static_cast<size_t>(dot - name));
    else
      cache_dir.append(name);

#ifdef ESP_PLATFORM
    DIR* d = opendir(cache_dir.c_str());
    if (d) {
      struct dirent* ent;
      char fpath[300];
      while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.')
          continue;
        std::snprintf(fpath, sizeof(fpath), "%s/%s", cache_dir.c_str(), ent->d_name);
        std::remove(fpath);
      }
      closedir(d);
      rmdir(cache_dir.c_str());
    }
#else
    if (fs::exists(cache_dir))
      fs::remove_all(cache_dir);
#endif
  }

  // Reset state and rescan
  button1_hold_ms_ = 0;
  delete_pending_ = false;
  delete_index_ = -1;
  title_ = "Microreader";
  scan_directory_(buf);
  populate_list_();
  draw_all_(buf, runtime.battery_percentage());
  buf.full_refresh();
}

void MainMenu::cancel_delete_(DrawBuffer& buf, IRuntime& runtime) {
  set_item_label(delete_index_, delete_book_label_);
  button1_hold_ms_ = 0;
  delete_pending_ = false;
  delete_index_ = -1;
  title_ = "Microreader";
  draw_all_(buf, runtime.battery_percentage());
  buf.refresh();
}

void MainMenu::scan_directory_(DrawBuffer& buf) {
  if (!books_dir_ || !app_->data_dir_)
    return;

  std::string root_dir = books_dir_;
  const std::string index_path = std::string(app_->data_dir_) + "/book_index.dat";

  buf.sync_bw_ram();

  BookIndex::instance().build_index(root_dir, buf);
  BookIndex::instance().save(index_path);

  // Refresh to clean up the loading bar
  buf.reset_after_scratch(true);
}

void MainMenu::populate_list_() {
  clear_items();
  entries_.clear();

  for (const auto& index_entry : BookIndex::instance().entries()) {
    BookEntry e;
    e.path = index_entry.path;

    if (list_format_ == BookListFormat::TitleOnly) {
      e.label = index_entry.title.empty() ? index_entry.label : index_entry.title;
    } else if (list_format_ == BookListFormat::Filename) {
      const char* name = index_entry.path.c_str();
      const char* sep = std::strrchr(name, '/');
#ifdef _WIN32
      const char* bsep = std::strrchr(name, '\\');
      if (bsep && (!sep || bsep > sep))
        sep = bsep;
#endif
      if (sep)
        name = sep + 1;

      const char* dot = std::strrchr(name, '.');
      if (dot) {
        e.label = std::string(name, dot - name);
      } else {
        e.label = name;
      }
    } else {
      e.label = index_entry.label;  // Title & Author
    }

    entries_.push_back(std::move(e));
    add_item(entries_.back().label);
  }

  // Restore previously selected book position — only on first visit.
  if (!initial_selection_.empty()) {
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
      if (entries_[i].path == initial_selection_) {
        set_selected(i);
        break;
      }
    }
    initial_selection_.clear();
  }
}

}  // namespace microreader
