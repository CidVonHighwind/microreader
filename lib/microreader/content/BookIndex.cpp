#include "BookIndex.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <queue>
#include <string>
#include <vector>

#include "../HeapLog.h"
#include "../display/DrawBuffer.h"
#include "Book.h"

#ifdef ESP_PLATFORM
#include <dirent.h>
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

namespace microreader {

BookIndex& BookIndex::instance() {
  static BookIndex instance;
  return instance;
}

void BookIndex::add_entry(std::string_view path, std::string_view title, std::string_view author,
                          uint32_t last_open_order) {
  BookIndexEntry entry;
  entry.path = pool_.add(path);
  entry.title = pool_.add(title);
  entry.author = pool_.add(author);
  entry.last_open_order = last_open_order;
  entries_.push_back(entry);
}

bool BookIndex::load(const std::string& index_file) {
  FILE* f = std::fopen(index_file.c_str(), "rb");
  if (!f)
    return false;

  entries_.clear();
  pool_.reset();

  char line[1024];
  while (std::fgets(line, sizeof(line), f) && static_cast<int>(entries_.size()) < MAX_BOOKS) {
    // Remove newline
    size_t len = std::strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
      line[len - 1] = '\0';
      len--;
    }
    if (len > 0 && line[len - 1] == '\r') {
      line[len - 1] = '\0';
      len--;
    }

    // Format: path|title|author[|last_open_order]
    char* sep1 = std::strchr(line, '|');
    if (!sep1)
      continue;
    *sep1 = '\0';
    char* sep2 = std::strchr(sep1 + 1, '|');
    if (!sep2)
      continue;
    *sep2 = '\0';

    // Optional 4th field: last_open_order
    uint32_t order = 0;
    char* sep3 = std::strchr(sep2 + 1, '|');
    if (sep3) {
      *sep3 = '\0';
      order = static_cast<uint32_t>(std::strtoul(sep3 + 1, nullptr, 10));
    }
    add_entry(line, sep1 + 1, sep2 + 1, order);
  }

  std::fclose(f);
  return true;
}

bool BookIndex::save(const std::string& index_file) const {
  FILE* f = std::fopen(index_file.c_str(), "wb");
  if (!f)
    return false;

  for (const auto& entry : entries_) {
    auto path_v = entry.path.view(pool_);
    auto title_v = entry.title.view(pool_);
    auto author_v = entry.author.view(pool_);
    std::fprintf(f, "%.*s|%.*s|%.*s|%u\n", static_cast<int>(path_v.size()), path_v.data(),
                 static_cast<int>(title_v.size()), title_v.data(), static_cast<int>(author_v.size()), author_v.data(),
                 static_cast<unsigned>(entry.last_open_order));
  }
  std::fclose(f);
  return true;
}

void BookIndex::clear_entries() {
  { std::vector<BookIndexEntry> tmp; entries_.swap(tmp); }
  pool_.reset();
}

void BookIndex::set_last_opened(std::string_view path, uint32_t order) {
  for (auto& entry : entries_) {
    if (entry.path.view(pool_) == path) {
      entry.last_open_order = order;
      return;
    }
  }
}

void BookIndex::remove_entry(int index) {
  if (index >= 0 && index < static_cast<int>(entries_.size()))
    entries_.erase(entries_.begin() + index);
}

void BookIndex::build_index(const std::string& root_dir, DrawBuffer& buf) {
  entries_.clear();
  pool_.reset();
  buf.show_loading("Scanning...", 0);
  // Process epub files as we find them to avoid storing all paths in memory.
  int done = 0;
  int total = 0;

  // Reuse one Book instance across all iterations so the ZipReader's internal
  // vectors (entries_, name_blob_) retain their allocated capacity — repeated
  // alloc/free of same-sized buffers fragments the heap on ESP32.
  Book book;

  // Helper to process a single epub path (keeps peak memory low)
  auto process_path = [&](const std::string& path) {
    if (static_cast<int>(entries_.size()) >= MAX_BOOKS)
      return;
    buf.show_loading("Indexing...", total > 0 ? 10 + (done * 90 / total) : 10);
    book.close();
    if (book.open(path.c_str(), buf.scratch_buf1(), buf.scratch_buf2(), false) == EpubError::Ok) {
      auto meta = book.metadata();
      add_entry(path, meta.title, meta.author.value_or(""));
    }
    book.close();
    done++;
  };

  // Single iterator helper: calls `cb(path)` for each .epub found
  auto iterate_epubs = [&](const std::function<void(const std::string&)>& cb) {
#ifdef ESP_PLATFORM
    std::queue<std::string> q;
    q.push(root_dir);
    while (!q.empty()) {
      std::string current_dir = q.front();
      q.pop();
      DIR* dir = opendir(current_dir.c_str());
      if (!dir)
        continue;
      struct dirent* ent;
      while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.')
          continue;
        std::string fullpath = current_dir + "/" + ent->d_name;
        if (ent->d_type == DT_DIR) {
          q.push(fullpath);
        } else {
          size_t len = std::strlen(ent->d_name);
          if (len > 5) {
            const char* ext = ent->d_name + len - 5;
            char ext_lower[6];
            for (int i = 0; i < 5; i++)
              ext_lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[i])));
            ext_lower[5] = '\0';
            if (std::strcmp(ext_lower, ".epub") == 0)
              cb(fullpath);
          }
        }
      }
      closedir(dir);
    }
#else
    try {
      for (const auto& entry : fs::recursive_directory_iterator(root_dir)) {
        if (!entry.is_regular_file())
          continue;
        auto ext = entry.path().extension().string();
        for (char& c : ext)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".epub") {
          std::string path_str = entry.path().string();
          for (char& c : path_str)
            if (c == '\\')
              c = '/';
          cb(path_str);
        }
      }
    } catch (...) {}
#endif
  };

  // Count then process using the iterator
  iterate_epubs([&](const std::string& p) { total++; });
  MR_LOGI("index", "Found %d epub(s), indexing...", total);
  iterate_epubs(process_path);
  MR_LOGI("index", "Indexed %d book(s)", done);

  if (done > 0)
    buf.show_loading("Indexing...", 100);
}

}  // namespace microreader
