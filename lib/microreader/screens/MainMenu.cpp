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
  title_ = "Sami";

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
  if (index == kFinishedRow) {
    app_->push_screen(ScreenId::Finished);
    return;
  }
  if (index == kWifiRow) {
    app_->push_screen(ScreenId::Wifi);
    return;
  }
  if (is_separator(index)) return;
  int real = entries_index_for(index);
  if (real < 0 || real >= static_cast<int>(entries_.size())) return;
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
  return_to_row_ = (selected() == kFinishedRow || selected() == kWifiRow) ? selected() : -1;

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
  int n = static_cast<int>(entries_.size()) + kTopRows;
  if (separator_visual_index_ >= 0) ++n;
  return n;
}

bool MainMenu::is_separator(int index) const {
  if (index == kTopRows - 1)
    return true;  // gap under the top rows
  return separator_visual_index_ >= 0 && index - kTopRows == separator_visual_index_;
}

std::string_view MainMenu::title_with_percent_(std::string_view title, int percent) const {
  char suffix[16];
  std::snprintf(suffix, sizeof(suffix), "  \xc2\xb7  %d%%", percent);
  label_buf_.assign(title.data(), title.size());
  label_buf_ += suffix;
  if (!ui_font_.valid())
    return label_buf_;
  // Same width limit ListMenuScreen uses for centred items.
  const int max_w = (buf_ ? buf_->width() : DrawBuffer::kWidth) - 48;
  if (ui_font_.word_width(label_buf_.data(), label_buf_.size(), FontStyle::Regular) <= max_w)
    return label_buf_;
  // Shorten the title (on a UTF-8 boundary) and add "..." before the percent.
  const std::string tail = std::string("...") + suffix;
  const int budget = max_w - ui_font_.word_width(tail.data(), tail.size(), FontStyle::Regular);
  size_t fit = 0;
  while (fit < title.size()) {
    const uint8_t b = static_cast<uint8_t>(title[fit]);
    const size_t cb = b < 0x80 ? 1u : b < 0xE0 ? 2u : b < 0xF0 ? 3u : 4u;
    if (fit + cb > title.size() || ui_font_.word_width(title.data(), fit + cb, FontStyle::Regular) > budget)
      break;
    fit += cb;
  }
  label_buf_.assign(title.data(), fit);
  label_buf_ += tail;
  return label_buf_;
}

std::string_view MainMenu::get_item_label(int index) const {
  if (index == kFinishedRow) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "Finished Books (%u)",
                  static_cast<unsigned>(app_ ? app_->finished_books().size() : 0));
    finished_label_ = buf;
    return finished_label_;
  }
  if (index == kWifiRow)
    return "Wi-Fi Transfer";
  if (is_separator(index)) return {};
  int real = entries_index_for(index);
  if (real < 0 || real >= static_cast<int>(entries_.size()))
    return {};
  const StringPool& pool = BookIndex::instance().pool();
  const BookEntry& e = entries_[real];
  if (list_format_ == BookListFormat::TitleOnly) {
    return e.title_ref.view(pool);
  } else if (list_format_ == BookListFormat::TitleAndPercent) {
    const std::string_view title = e.title_ref.view(pool);
    const int pct = app_ ? app_->book_progress().get(e.path, title, e.author_ref.view(pool)) : -1;
    if (pct < 0)
      return title;  // never opened
    return title_with_percent_(title, pct);
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

  bool matched = false;
  if (!initial_selection_.empty()) {
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
      if (entries_[i].path == initial_selection_) {
        set_selected(visual_for_entries(i));
        matched = true;
        break;
      }
    }
    initial_selection_.clear();
  }
  if (!matched) {
    // Back from the Finished shelf or Wi-Fi Transfer: stay on that row.
    // Otherwise start on the first book rather than the top rows.
    if (return_to_row_ >= 0)
      set_selected(return_to_row_);
    else if (entries_.empty())
      set_selected(kFinishedRow);
    else
      set_selected(kTopRows);
  }
  return_to_row_ = -1;
}

}  // namespace microreader
