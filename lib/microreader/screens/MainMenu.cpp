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

static bool ci_less(std::string_view a, std::string_view b) {
  size_t min_len = std::min(a.size(), b.size());
#ifdef _WIN32
  int cmp = _strnicmp(a.data(), b.data(), min_len);
#else
  int cmp = strncasecmp(a.data(), b.data(), min_len);
#endif
  if (cmp != 0) return cmp < 0;
  return a.size() < b.size();
}

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
    needs_scan_ = true;
  }
  cached_generation_ = BookIndex::instance().generation();
}

void MainMenu::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) {
  // Detect external mutations (serial upload/delete/rename) while this screen
  // is visible. The generation counter is bumped by BookIndex on every
  // mutation that changes the logical contents.
  if (cached_generation_ != BookIndex::instance().generation()) {
    cached_generation_ = BookIndex::instance().generation();
    populate_list_();
    draw_all_(buf, runtime.battery_percentage());
    buf.full_refresh();
  }

  if (needs_scan_) {
    needs_scan_ = false;
    scan_directory_(buf);
    populate_list_();

    draw_all_(buf, runtime.battery_percentage());
    buf.full_refresh();
    cached_generation_ = BookIndex::instance().generation();
  }

  ListMenuScreen::update(buttons, buf, runtime);
}

void MainMenu::on_select(int index) {
  if (is_separator(index)) return;
  int real = entries_index_for(index);
  last_selected_path_ = entries_[real].path;
  app_->record_book_opened(entries_[real].path);
  app_->reader()->set_path(entries_[real].path.c_str());
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
  int n = static_cast<int>(entries_.size());
  if (separator_visual_index_ >= 0) ++n;
  return n;
}

bool MainMenu::is_separator(int index) const {
  return separator_visual_index_ >= 0 && index == separator_visual_index_;
}

std::string_view MainMenu::get_item_label(int index) const {
  if (is_separator(index)) return {};
  int real = entries_index_for(index);
  if (real < 0 || real >= static_cast<int>(entries_.size()))
    return {};
  const StringPool& pool = BookIndex::instance().pool();
  const BookEntry& e = entries_[real];
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
  separator_visual_index_ = -1;

  const StringPool& bpool = BookIndex::instance().pool();
  const FinishedBooks& finished = app_->finished_books();
  for (const auto& idx : BookIndex::instance().entries()) {
    // Finished books live on their own shelf (Settings → Finished Books).
    if (finished.size() > 0 &&
        finished.contains(idx.path.view(bpool), idx.title.view(bpool), idx.author.view(bpool)))
      continue;
    BookEntry e;
    e.path = idx.path.to_string(bpool);
    e.title_ref = idx.title;
    e.author_ref = idx.author;
    e.last_open_order = idx.last_open_order;
    entries_.push_back(std::move(e));
  }

  if (sort_order_ == BookSortOrder::LastOpened) {
    const auto fmt = list_format_;
    std::stable_sort(entries_.begin(), entries_.end(),
                     [&bpool, fmt](const BookEntry& a, const BookEntry& b) {
                      if (a.last_open_order != b.last_open_order)
                        return a.last_open_order > b.last_open_order;
                      if (fmt == BookListFormat::Filename)
                        return ci_less(filename_sv(a.path), filename_sv(b.path));
                      return ci_less(a.title_ref.view(bpool), b.title_ref.view(bpool));
                     });
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
      if (i > 0 && entries_[i].last_open_order == 0 &&
          entries_[i - 1].last_open_order > 0) {
        separator_visual_index_ = i;
        break;
      }
    }
  } else if (list_format_ == BookListFormat::Filename) {
    std::stable_sort(entries_.begin(), entries_.end(),
                      [](const BookEntry& a, const BookEntry& b) { return ci_less(filename_sv(a.path), filename_sv(b.path)); });
  } else {
    std::stable_sort(entries_.begin(), entries_.end(),
                     [&bpool](const BookEntry& a, const BookEntry& b) {
                        return ci_less(a.title_ref.view(bpool), b.title_ref.view(bpool));
                     });
  }

  if (!initial_selection_.empty()) {
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
      if (entries_[i].path == initial_selection_) {
        set_selected(visual_for_entries(i));
        break;
      }
    }
    initial_selection_.clear();
  }
}

}  // namespace microreader
