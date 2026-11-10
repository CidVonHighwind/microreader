#include "SleepScreens.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "../HeapLog.h"
#include "../content/Book.h"
#include "../content/ImageDecoder.h"
#include "../content/ZipReader.h"
#include "../display/DrawBuffer.h"
#include "ListMenuScreen.h"

#ifdef ESP_PLATFORM
#include <dirent.h>
#include <sys/stat.h>
#else
#include <filesystem>
#endif

namespace microreader {
namespace sleep_screens {

namespace {

using UiFont = ListMenuScreen::UiFont;

const BitmapFont& ui(UiFont f) {
  return ListMenuScreen::shared_font(f);
}

int text_w(const BitmapFont& f, std::string_view s) {
  return f.word_width(s.data(), s.size(), FontStyle::Regular);
}

void text_at(DrawBuffer& buf, const BitmapFont& f, int x, int top, std::string_view s) {
  buf.draw_text_proportional(x, top + f.baseline(), s.data(), s.size(), f, false);
}

// Text clipped to max_w with a trailing "..." when it doesn't fit.
std::string fit_text(const BitmapFont& f, std::string_view s, int max_w) {
  if (text_w(f, s) <= max_w)
    return std::string(s);
  const int budget = max_w - text_w(f, "...");
  size_t fit = 0;
  while (fit < s.size()) {
    const uint8_t b = static_cast<uint8_t>(s[fit]);
    const size_t cb = b < 0x80 ? 1u : b < 0xE0 ? 2u : b < 0xF0 ? 3u : 4u;
    if (fit + cb > s.size() || text_w(f, s.substr(0, fit + cb)) > budget)
      break;
    fit += cb;
  }
  return std::string(s.substr(0, fit)) + "...";
}

bool ends_with_ci(std::string_view s, std::string_view suffix) {
  if (s.size() < suffix.size())
    return false;
  for (size_t i = 0; i < suffix.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(s[s.size() - suffix.size() + i])) !=
        std::tolower(static_cast<unsigned char>(suffix[i])))
      return false;
  return true;
}

bool is_raster_image(std::string_view name) {
  return ends_with_ci(name, ".jpg") || ends_with_ci(name, ".jpeg") || ends_with_ci(name, ".png");
}

std::string base_name(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string stem(const std::string& path) {
  std::string b = base_name(path);
  const size_t dot = b.rfind('.');
  return dot == std::string::npos ? b : b.substr(0, dot);
}

void make_dirs(const std::string& dir) {
#ifdef ESP_PLATFORM
  // One level at a time (FatFS mkdir isn't recursive).
  for (size_t i = 1; i <= dir.size(); ++i)
    if (i == dir.size() || dir[i] == '/')
      mkdir(dir.substr(0, i).c_str(), 0775);
#else
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
#endif
}

// Cached frames are the raw panel buffer (DrawBuffer::kBufSize bytes) drawn
// in portrait (Deg90) orientation.
bool load_frame(DrawBuffer& buf, const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f)
    return false;
  const size_t n = std::fread(buf.render_buf(), 1, DrawBuffer::kBufSize, f);
  std::fclose(f);
  if (n != DrawBuffer::kBufSize) {
    buf.fill(true);
    return false;
  }
  return true;
}

void save_frame(DrawBuffer& buf, const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f)
    return;
  const bool ok = std::fwrite(buf.render_buf(), 1, DrawBuffer::kBufSize, f) == DrawBuffer::kBufSize;
  std::fclose(f);
  if (!ok)
    std::remove(path.c_str());
}

// Presents a plain file as a "stored" ZIP entry so the streaming EPUB image
// decoders can read it. ZipEntry::data_offset == 0 means "parse the local
// header", so the file is exposed shifted by one byte and the entry's data
// starts at offset 1.
class PlainImageFile final : public IZipFile {
 public:
  bool open(const std::string& path) {
    path_ = path;
    if (!f_.open(path.c_str()))
      return false;
    if (!f_.seek(0, SEEK_END))
      return false;
    size_ = f_.tell();
    return size_ > 0 && f_.seek(0, SEEK_SET);
  }
  ZipEntry entry() const {
    ZipEntry e;
    e.name = path_;  // extension drives format detection
    e.compression = 0;
    e.uncompressed_size = e.compressed_size = static_cast<uint32_t>(size_);
    e.data_offset = 1;
    return e;
  }
  bool seek(int64_t offset, int whence) override {
    return f_.seek(whence == SEEK_SET ? offset - 1 : offset, whence);
  }
  int64_t tell() override {
    return f_.tell() + 1;
  }
  size_t read(void* buf, size_t size) override {
    return f_.read(buf, size);
  }

 private:
  StdioZipFile f_;
  std::string path_;
  int64_t size_ = 0;
};

// Where decoded pixels land on screen.
struct DrawTarget {
  DrawBuffer* buf;
  int x0, y0;      // top-left of the placed image (logical coordinates)
  int out_h;       // decoded image height (before rotation)
  bool rotate;     // rotate 90° clockwise while drawing
  void plot_black(int x, int y) const {
    if (rotate)
      buf->set_pixel(x0 + (out_h - 1 - y), y0 + x, false);
    else
      buf->set_pixel(x0 + x, y0 + y, false);
  }
};

void emit_row(void* ctx, uint16_t y, const uint8_t* data, uint16_t width) {
  const auto* t = static_cast<const DrawTarget*>(ctx);
  for (int x = 0; x < width; ++x)
    if (!((data[x >> 3] >> (7 - (x & 7))) & 1))  // 1 = white
      t->plot_black(x, y);
}

void emit_pixel(void* ctx, uint16_t x, uint16_t y, bool white) {
  if (!white)
    static_cast<const DrawTarget*>(ctx)->plot_black(x, y);
}

bool read_entry_image_size(IZipFile& file, const ZipEntry& entry, uint8_t* work, size_t work_size, uint16_t& w,
                           uint16_t& h) {
  ZipEntryInput in;
  if (in.open(file, entry, work, work_size) != ZipError::Ok)
    return false;
  ImageSizeStream s;
  uint8_t chunk[512];
  size_t total = 0;
  while (total < 1024 * 1024) {  // JPEG SOF sits after EXIF/ICC data; 1 MB is plenty
    const size_t n = in.read(chunk, sizeof(chunk));
    if (n == 0)
      break;
    total += n;
    if (s.feed(chunk, n))
      break;
  }
  if (!s.ok() || s.width() == 0 || s.height() == 0)
    return false;
  w = s.width();
  h = s.height();
  return true;
}

}  // namespace

bool draw_image_entry(DrawBuffer& buf, IZipFile& file, const ZipEntry& entry, int x, int y, int w, int h,
                      bool allow_rotate, unsigned char* work, size_t work_size) {
  uint16_t sw = 0, sh = 0;
  if (!read_entry_image_size(file, entry, work, work_size, sw, sh))
    return false;

  // Turn landscape images to fill a portrait box.
  const bool rotate = allow_rotate && sw > sh && h > w;
  const int fit_w = rotate ? h : w;
  const int fit_h = rotate ? w : h;
  int ow, oh;
  if (static_cast<int64_t>(sw) * fit_h > static_cast<int64_t>(sh) * fit_w) {
    ow = fit_w;
    oh = std::max(1, static_cast<int>(static_cast<int64_t>(sh) * fit_w / sw));
  } else {
    oh = fit_h;
    ow = std::max(1, static_cast<int>(static_cast<int64_t>(sw) * fit_h / sh));
  }
  const int placed_w = rotate ? oh : ow;
  const int placed_h = rotate ? ow : oh;

  DrawTarget target{&buf, x + (w - placed_w) / 2, y + (h - placed_h) / 2, oh, rotate};
  ImageRowSink rows{emit_row, &target};
  ImagePixelSink pixels{emit_pixel, &target};
  DecodedImage out;
  const ImageError err = decode_image_from_entry(file, entry, static_cast<uint16_t>(ow), static_cast<uint16_t>(oh),
                                                 out, work, work_size, /*scale_to_fill=*/true, &rows, &pixels);
  if (err != ImageError::Ok)
    MR_LOGI("sleep", "image decode failed (%d), %ux%u", static_cast<int>(err), static_cast<unsigned>(sw),
            static_cast<unsigned>(sh));
  return err == ImageError::Ok;
}

std::vector<std::string> list_sd_images(bool with_converted) {
  std::vector<std::string> out;
#ifdef ESP_PLATFORM
  static const char* const kDirs[] = {"/sdcard/.sleep", "/sdcard/sleep"};
#else
  static const char* const kDirs[] = {"sd/.sleep", "sd/sleep"};
#endif
  auto consider = [&](const std::string& dir, const std::string& name) {
    if (name.empty() || name[0] == '.')
      return;
    const std::string path = dir + "/" + name;
    if (ends_with_ci(name, ".mgr"))
      out.push_back(path);
    else if (with_converted && ends_with_ci(name, ".bmp"))
      out.push_back("bmp:" + path);
    else if (with_converted && is_raster_image(name))
      out.push_back("img:" + path);
  };
  for (const char* dir : kDirs) {
#ifdef ESP_PLATFORM
    DIR* d = opendir(dir);
    if (!d)
      continue;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr)
      consider(dir, ent->d_name);
    closedir(d);
#else
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
      consider(dir, it->path().filename().string());
#endif
  }
  std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
    return base_name(a) < base_name(b);
  });
  return out;
}

void draw_sleeping_label(DrawBuffer& buf) {
  const BitmapFont& small = ui(UiFont::Small);
  static const char kText[] = "sleeping...";
  const int tw = text_w(small, kText);
  const int bh = small.y_advance() + 4;
  const int bx = (buf.width() - tw) / 2 - 8;
  const int by = buf.height() - bh - 6;
  buf.fill_rect(bx, by, tw + 16, bh, true);
  text_at(buf, small, bx + 8, by + 2, kText);
}

bool draw_image_file(DrawBuffer& buf, const std::string& path, const char* data_dir) {
  std::string cache;
  if (data_dir && *data_dir) {
    const std::string dir = std::string(data_dir) + "/cache/sleep";
    make_dirs(dir);
    cache = dir + "/" + base_name(path) + ".frame";
    if (load_frame(buf, cache))
      return true;
  }

  PlainImageFile file;
  if (!file.open(path)) {
    MR_LOGI("sleep", "cannot open image %s", path.c_str());
    return false;
  }
  const ZipEntry entry = file.entry();
  buf.fill(true);
  // Draw into the inactive buffer; the active one is free to use as the
  // decoder's read-ahead buffer (the panel is fully redrawn afterwards).
  if (!draw_image_entry(buf, file, entry, 0, 0, buf.width(), buf.height(), /*allow_rotate=*/true,
                        buf.scratch_buf2(), DrawBuffer::kBufSize)) {
    buf.fill(true);
    return false;
  }
  if (!cache.empty())
    save_frame(buf, cache);
  return true;
}

bool draw_book_cover(DrawBuffer& buf, const std::string& book_path, const std::string& title, int progress_pct,
                     const char* data_dir) {
  const int W = buf.width();
  const int H = buf.height();
  constexpr int kStripH = 92;  // title + progress underneath the cover
  const int cover_h = H - kStripH;

  std::string cache;
  if (data_dir && *data_dir) {
    const std::string dir = std::string(data_dir) + "/cache/" + stem(book_path);
    make_dirs(dir);
    cache = dir + "/cover-v1.frame";
  }

  if (cache.empty() || !load_frame(buf, cache)) {
    Book book;
    // Opening uses both scratch buffers; they're free again afterwards.
    if (book.open(book_path.c_str(), buf.scratch_buf1(), buf.scratch_buf2(), /*parse_css_ncx=*/false) !=
        EpubError::Ok) {
      MR_LOGI("sleep", "cover: cannot open %s", book_path.c_str());
      return false;
    }
    const Epub& epub = book.epub();
    int idx = epub.cover_entry_index();
    if (idx < 0) {
      // No declared cover: fall back to an image whose name says "cover".
      for (size_t i = 0; i < epub.zip().entry_count() && idx < 0; ++i) {
        std::string lower(epub.zip().entry(i).name);
        for (char& c : lower)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.find("cover") != std::string::npos && is_raster_image(lower))
          idx = static_cast<int>(i);
      }
    }
    if (idx < 0) {
      MR_LOGI("sleep", "cover: none found in %s", book_path.c_str());
      return false;
    }
    const ZipEntry entry = epub.zip().entry(static_cast<size_t>(idx));
    buf.fill(true);
    if (!draw_image_entry(buf, book.file(), entry, 0, 0, W, cover_h, /*allow_rotate=*/false, buf.scratch_buf2(),
                          DrawBuffer::kBufSize)) {
      buf.fill(true);
      return false;
    }
    if (!cache.empty())
      save_frame(buf, cache);
  }

  // Title and progress under the cover.
  const BitmapFont& medium = ui(UiFont::Medium);
  const BitmapFont& large = ui(UiFont::Large);
  const int margin = 24;
  const int pct = std::max(0, std::min(100, progress_pct));
  char pct_text[8];
  std::snprintf(pct_text, sizeof(pct_text), "%d%%", pct);
  const int pct_w = text_w(large, pct_text);
  const int top = cover_h + 8;
  buf.fill_rect(0, cover_h, W, H - cover_h, true);
  const std::string shown = fit_text(medium, title.empty() ? stem(book_path) : title, W - 2 * margin - pct_w - 16);
  text_at(buf, medium, margin, top + 4, shown);
  text_at(buf, large, W - margin - pct_w, top, pct_text);

  const int bar_y = top + large.y_advance() + 6;
  const int bar_w = W - 2 * margin;
  constexpr int kBarH = 10;
  buf.fill_rect(margin, bar_y, bar_w, 1, false);
  buf.fill_rect(margin, bar_y + kBarH - 1, bar_w, 1, false);
  buf.fill_rect(margin, bar_y, 1, kBarH, false);
  buf.fill_rect(margin + bar_w - 1, bar_y, 1, kBarH, false);
  const int fill = (bar_w - 4) * pct / 100;
  if (fill > 0)
    buf.fill_rect(margin + 2, bar_y + 2, fill, kBarH - 4, false);

  const BitmapFont& small = ui(UiFont::Small);
  static const char kSleeping[] = "sleeping...";
  text_at(buf, small, (W - text_w(small, kSleeping)) / 2, H - small.y_advance() - 4, kSleeping);
  return true;
}

}  // namespace sleep_screens
}  // namespace microreader
