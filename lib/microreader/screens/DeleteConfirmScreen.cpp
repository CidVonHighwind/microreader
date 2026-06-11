#include "DeleteConfirmScreen.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "../Application.h"
#include "../content/BookIndex.h"

#ifdef ESP_PLATFORM
#include <dirent.h>
#include <sys/stat.h>
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

namespace microreader {

static std::vector<std::string> wrap_text(const std::string& text,
                                           const BitmapFont& font,
                                           int max_w) {
  std::vector<std::string> lines;
  if (text.empty() || max_w <= 0 || !font.valid())
    return lines;

  const char* p = text.c_str();
  const char* start = p;
  const char* last_break = nullptr;
  int line_w = 0;

  while (*p) {
    const uint8_t b = static_cast<uint8_t>(*p);
    const size_t cb = b < 0x80 ? 1u : b < 0xE0 ? 2u : b < 0xF0 ? 3u : 4u;
    const int char_w = font.word_width(p, cb, FontStyle::Regular);

    if (line_w + char_w > max_w && p > start) {
      const char* break_at = (last_break && last_break > start) ? last_break : p;
      lines.push_back(std::string(start, break_at - start));
      start = break_at;
      line_w = 0;
      last_break = nullptr;
      if (break_at < p) {
        p = break_at;
        continue;
      }
    }

    if (cb == 1 && (*p == ' ' || *p == '-' || *p == '_' || *p == '.'))
      last_break = p + cb;

    line_w += char_w;
    p += cb;
  }

  if (p > start)
    lines.push_back(std::string(start, p - start));

  return lines;
}

void DeleteConfirmScreen::on_start() {
  title_ = "Delete Book?";

  const int max_w = buffer_width() - 32;
  auto lines = wrap_text(filename_, ui_font_, max_w);
  filename_lines_ = static_cast<int>(lines.size());

  for (const auto& line : lines)
    add_separator(line);
  add_separator("");
  delete_idx_ = count();
  add_item("Delete");
  cancel_idx_ = count();
  add_item("Cancel");
  set_selected(cancel_idx_);
}

void DeleteConfirmScreen::on_select(int index) {
  if (index == delete_idx_) {
    delete_book_();
    app_->pop_screen();
  } else {
    app_->pop_screen();
  }
}

void DeleteConfirmScreen::delete_book_() {
  std::remove(book_path_.c_str());

  const char* data_dir = app_->data_dir_;
  if (data_dir) {
    // Remove cache directory for this book
    // Cache path: <data_dir>/cache/<filename_without_ext>/book.mrb
    const char* name = book_path_.c_str();
    const char* sep = std::strrchr(name, '/');
#ifdef _WIN32
    const char* bsep = std::strrchr(name, '\\');
    if (bsep && (!sep || bsep > sep))
      sep = bsep;
#endif
    if (sep)
      name = sep + 1;
    const char* dot = std::strrchr(name, '.');
    size_t name_len = dot ? static_cast<size_t>(dot - name) : std::strlen(name);

    std::string cache_dir = std::string(data_dir) + "/cache/" + std::string(name, name_len);

#ifdef ESP_PLATFORM
    DIR* cd = opendir(cache_dir.c_str());
    if (cd) {
      struct dirent* ent;
      char file_path[768];
      while ((ent = readdir(cd)) != nullptr) {
        if (ent->d_name[0] == '.')
          continue;
        std::snprintf(file_path, sizeof(file_path), "%s/%s", cache_dir.c_str(), ent->d_name);
        std::remove(file_path);
      }
      closedir(cd);
      rmdir(cache_dir.c_str());
    }
#else
    try {
      fs::remove_all(cache_dir);
    } catch (...) {}
#endif

    // Reload BookIndex from file because MainMenu::stop() calls
    // clear_entries() when the menu is paused.
    std::string index_path = std::string(data_dir) + "/book_index.dat";
    BookIndex::instance().load(index_path);

    // Remove entry from BookIndex and re-save
    auto& index = BookIndex::instance();
    const StringPool& pool = index.pool();
    const auto& entries = index.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
      if (entries[i].path.view(pool) == book_path_) {
        index.remove_entry(static_cast<int>(i));
        break;
      }
    }
    index.save(index_path);
  }
}

}  // namespace microreader
