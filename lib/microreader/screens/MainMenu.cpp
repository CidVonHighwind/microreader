#include "MainMenu.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../Application.h"
#include "../content/BookIndex.h"

#ifdef ESP_PLATFORM
#include <dirent.h>
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

namespace microreader {

// Returns a view into `path` pointing at the bare filename without extension.
static std::string_view filename_sv(const std::string& path) {
  const char* name = path.c_str();
  const char* sep = std::strrchr(name, '/');
#ifdef _WIN32
  const char* bsep = std::strrchr(name, '\\');
  if (bsep && (!sep || bsep > sep))
    sep = bsep;
#endif
  if (sep)
    name = sep + 1;
  const char* dot = std::strrchr(name, '.');
  size_t len = dot ? static_cast<size_t>(dot - name) : std::strlen(name);
  return {name, len};
}

void MainMenu::on_start() {
  title_ = "Microreader";
  set_long_back_threshold(20);

  if (!app_->data_dir_) {
    needs_scan_ = false;
    return;
  }

  std::string index_path = std::string(app_->data_dir_) + "/book_index.dat";

  if (BookIndex::instance().load(index_path)) {
    populate_list_();
    needs_scan_ = false;
  } else {
    needs_scan_ = true;
  }
}

void MainMenu::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) {
  if (needs_scan_) {
    needs_scan_ = false;
    scan_directory_(buf);
    populate_list_();

    draw_all_(buf, runtime.battery_percentage());
    buf.full_refresh();
  }

  ListMenuScreen::update(buttons, buf, runtime);
}

void MainMenu::on_select(int index) {
  last_selected_path_ = entries_[index].path;
  app_->record_book_opened(entries_[index].path);
  app_->reader()->set_path(entries_[index].path.c_str());
  app_->push_screen(ScreenId::Reader);
}

void MainMenu::stop() {
  const std::string& cur = current_book_path();
  if (!cur.empty()) {
    initial_selection_ = cur;
    last_selected_path_ = cur;
  }

  { std::vector<BookEntry> tmp; entries_.swap(tmp); }
  free_items_storage();
  BookIndex::instance().clear_entries();
}

void MainMenu::on_back() {
  app_->push_screen(ScreenId::Settings);
}

void MainMenu::on_long_back(int index) {
  if (index < 0 || index >= static_cast<int>(entries_.size()))
    return;
  app_->delete_confirm()->setup(entries_[index].path);
  app_->push_screen(ScreenId::DeleteConfirm);
}

void MainMenu::scan_directory_(DrawBuffer& buf) {
  if (!books_dir_ || !app_->data_dir_)
    return;

  std::string root_dir = books_dir_;
  const std::string index_path = std::string(app_->data_dir_) + "/book_index.dat";

  buf.sync_bw_ram();

  BookIndex::instance().build_index(root_dir, buf);
  BookIndex::instance().save(index_path);

  buf.reset_after_scratch(true);
}

int MainMenu::count() const {
  return static_cast<int>(entries_.size());
}

std::string_view MainMenu::get_item_label(int index) const {
  if (index < 0 || index >= static_cast<int>(entries_.size()))
    return {};
  const StringPool& pool = BookIndex::instance().pool();
  const BookEntry& e = entries_[index];
  if (list_format_ == BookListFormat::TitleOnly) {
    return e.title_ref.view(pool);
  } else if (list_format_ == BookListFormat::Filename) {
    return filename_sv(e.path);
  } else {
    label_buf_ = std::string(e.title_ref.view(pool));
    label_buf_ += " - ";
    label_buf_ += e.author_ref.view(pool);
    return std::string_view(label_buf_);
  }
}

void MainMenu::populate_list_() {
  clear_items();
  entries_.clear();

  const StringPool& bpool = BookIndex::instance().pool();
  for (const auto& idx : BookIndex::instance().entries()) {
    BookEntry e;
    e.path = idx.path.to_string(bpool);
    e.title_ref = idx.title;
    e.author_ref = idx.author;
    e.last_open_order = idx.last_open_order;
    entries_.push_back(std::move(e));
  }

  if (sort_order_ == BookSortOrder::ByLastOpened) {
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const BookEntry& a, const BookEntry& b) { return a.last_open_order > b.last_open_order; });
  } else if (list_format_ == BookListFormat::Filename) {
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const BookEntry& a, const BookEntry& b) { return filename_sv(a.path) < filename_sv(b.path); });
  } else {
    std::stable_sort(entries_.begin(), entries_.end(),
                     [&bpool](const BookEntry& a, const BookEntry& b) {
                       return a.title_ref.view(bpool) < b.title_ref.view(bpool);
                     });
  }

  // Restore cursor to the saved path.
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
