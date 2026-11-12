#include "HomeScreen.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "../Application.h"
#include "../HeapLog.h"
#include "../content/Book.h"
#include "../content/BookIndex.h"
#include "../content/ZipReader.h"
#include "ListMenuScreen.h"
#include "SleepScreens.h"

#ifdef ESP_PLATFORM
#include <sys/stat.h>
#else
#include <filesystem>
#endif

namespace microreader {

namespace {

using UiFont = ListMenuScreen::UiFont;

// Same spacing as the list screens (ListMenuScreen.cpp), so the header and
// the bottom bar line up when moving between the home page and the lists.
constexpr int kHeaderY = 15;
constexpr int kHeaderBottomGap = 4;
constexpr int kBottomAreaH = 19;

constexpr int kSelFrame = 3;  // thickness of the selection frame around a cover
constexpr int kSelGap = 2;    // white gap between the cover and that frame

// Thumbnail file: "SMT1", width (u16 LE), height (u16 LE), then height rows
// of (width + 7) / 8 bytes, MSB first, 1 = white. 0 x 0 = the book has no
// usable cover (so it isn't decoded again every time).
constexpr char kThumbMagic[4] = {'S', 'M', 'T', '1'};

int text_w(const BitmapFont& f, std::string_view s) {
  return f.word_width(s.data(), s.size(), FontStyle::Regular);
}

void text_at(DrawBuffer& buf, const BitmapFont& f, int x, int top, std::string_view s, bool white = false) {
  buf.draw_text_proportional(x, top + f.baseline(), s.data(), s.size(), f, white);
}

size_t utf8_len(uint8_t b) {
  return b < 0x80 ? 1u : b < 0xE0 ? 2u : b < 0xF0 ? 3u : 4u;
}

// Text clipped to max_w with a trailing "..." when it doesn't fit.
std::string fit_text(const BitmapFont& f, std::string_view s, int max_w) {
  if (text_w(f, s) <= max_w)
    return std::string(s);
  const int budget = max_w - text_w(f, "...");
  size_t fit = 0;
  while (fit < s.size()) {
    const size_t cb = utf8_len(static_cast<uint8_t>(s[fit]));
    if (fit + cb > s.size() || text_w(f, s.substr(0, fit + cb)) > budget)
      break;
    fit += cb;
  }
  return std::string(s.substr(0, fit)) + "...";
}

std::string stem_of(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  std::string b = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = b.rfind('.');
  return dot == std::string::npos ? b : b.substr(0, dot);
}

void make_dirs(const std::string& dir) {
#ifdef ESP_PLATFORM
  for (size_t i = 1; i <= dir.size(); ++i)
    if (i == dir.size() || dir[i] == '/')
      mkdir(dir.substr(0, i).c_str(), 0775);
#else
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
#endif
}

bool file_exists(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f)
    return false;
  std::fclose(f);
  return true;
}

void write_empty_thumb(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f)
    return;
  const uint8_t zero[4] = {0, 0, 0, 0};
  std::fwrite(kThumbMagic, 1, 4, f);
  std::fwrite(zero, 1, 4, f);
  std::fclose(f);
}

// Save the rectangle (x, y, w, h) of the drawing buffer as a thumbnail.
bool write_thumb(DrawBuffer& buf, const std::string& path, int x, int y, int w, int h) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f)
    return false;
  const uint8_t hdr[4] = {static_cast<uint8_t>(w & 0xFF), static_cast<uint8_t>(w >> 8),
                          static_cast<uint8_t>(h & 0xFF), static_cast<uint8_t>(h >> 8)};
  bool ok = std::fwrite(kThumbMagic, 1, 4, f) == 4 && std::fwrite(hdr, 1, 4, f) == 4;
  uint8_t row[128];
  const int stride = (w + 7) / 8;
  for (int r = 0; ok && r < h; ++r) {
    std::memset(row, 0, sizeof(row));
    for (int c = 0; c < w; ++c)
      if (buf.get_pixel(x + c, y + r))
        row[c >> 3] |= static_cast<uint8_t>(0x80u >> (c & 7));
    ok = std::fwrite(row, 1, stride, f) == static_cast<size_t>(stride);
  }
  std::fclose(f);
  if (!ok)
    std::remove(path.c_str());
  return ok;
}

// Draw a thumbnail with its bottom-centre at (cx, bottom). Fills rect (x, y,
// w, h) with where it went. False if the file is missing or has no cover.
bool draw_thumb(DrawBuffer& buf, const std::string& path, int cx, int bottom, int max_w, int max_h, int* rect) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f)
    return false;
  uint8_t hdr[8];
  bool ok = std::fread(hdr, 1, 8, f) == 8 && std::memcmp(hdr, kThumbMagic, 4) == 0;
  const int w = ok ? (hdr[4] | (hdr[5] << 8)) : 0;
  const int h = ok ? (hdr[6] | (hdr[7] << 8)) : 0;
  const int stride = (w + 7) / 8;
  if (!ok || w <= 0 || h <= 0 || w > max_w || h > max_h || stride > 128) {
    std::fclose(f);
    return false;
  }
  const int x = cx - w / 2;
  const int y = bottom - h;
  uint8_t row[128];
  for (int r = 0; r < h; ++r) {
    if (std::fread(row, 1, stride, f) != static_cast<size_t>(stride))
      break;
    buf.blit_1bit_row(x, y + r, row, w);
  }
  std::fclose(f);
  rect[0] = x;
  rect[1] = y;
  rect[2] = w;
  rect[3] = h;
  return true;
}

void frame(DrawBuffer& buf, int x, int y, int w, int h, int t) {
  buf.fill_rect(x, y, w, t, false);
  buf.fill_rect(x, y + h - t, w, t, false);
  buf.fill_rect(x, y, t, h, false);
  buf.fill_rect(x + w - t, y, t, h, false);
}

// A centred text link; the selected one is drawn white on black like a
// selected list row.
void draw_link(DrawBuffer& buf, const BitmapFont& f, int y, std::string_view text, bool selected) {
  const int W = buf.width();
  const std::string shown = fit_text(f, text, W - 48);
  const int iw = text_w(f, shown);
  const int ix = (W - iw) / 2;
  if (selected) {
    constexpr int kPad = 6;
    const int top = y - 1;
    const int h = f.y_advance() + 2;
    buf.fill_rect(ix - kPad, top, iw + kPad * 2, h, false);
    buf.fill_rect(ix - kPad - 1, top + 1, 1, h - 2, false);
    buf.fill_rect(ix + iw + kPad, top + 1, 1, h - 2, false);
    text_at(buf, f, ix, y, shown, true);
  } else {
    text_at(buf, f, ix, y, shown, false);
  }
}

}  // namespace

std::string HomeScreen::thumb_path(const char* data_dir, const std::string& book_path, int box_w, int box_h) {
  if (!data_dir || !*data_dir)
    return {};
  char name[40];
  std::snprintf(name, sizeof(name), "/thumb-%dx%d.bin", box_w, box_h);
  return std::string(data_dir) + "/cache/" + stem_of(book_path) + name;
}

// ---------------------------------------------------------------------------
// Book list
// ---------------------------------------------------------------------------

void HomeScreen::load_books_() {
  books_.clear();
  needs_scan_ = false;
  if (!app_ || !app_->data_dir_)
    return;

  const std::string index_path = std::string(app_->data_dir_) + "/book_index.dat";
  BookIndex& index = BookIndex::instance();
  if (!index.load(index_path)) {
    // No index yet (first start): build it in update(), like the book list.
    needs_scan_ = app_->main_menu() && app_->main_menu()->has_books_dir();
    return;
  }
  cached_generation_ = index.generation();

  // The books opened most recently, newest first, leaving out finished ones.
  const StringPool& pool = index.pool();
  const FinishedBooks& finished = app_->finished_books();
  std::vector<const BookIndexEntry*> opened;
  for (const auto& e : index.entries()) {
    if (e.last_open_order == 0)
      continue;
    if (finished.size() > 0 && finished.contains(e.path.view(pool), e.title.view(pool), e.author.view(pool)))
      continue;
    opened.push_back(&e);
  }
  std::sort(opened.begin(), opened.end(), [](const BookIndexEntry* a, const BookIndexEntry* b) {
    return a->last_open_order > b->last_open_order;
  });
  for (const BookIndexEntry* e : opened) {
    if (static_cast<int>(books_.size()) >= kMaxBooks)
      break;
    RecentBook b;
    b.path = e->path.to_string(pool);
    b.title = e->title.to_string(pool);
    b.author = e->author.to_string(pool);
    b.percent = app_->book_progress().get(b.path, b.title, b.author);
    if (b.title.empty())
      b.title = stem_of(b.path);
    books_.push_back(std::move(b));
  }
}

void HomeScreen::scan_(DrawBuffer& buf) {
  MainMenu* menu = app_ ? app_->main_menu() : nullptr;
  if (!menu || !menu->has_books_dir() || !app_->data_dir_)
    return;
  const std::string index_path = std::string(app_->data_dir_) + "/book_index.dat";
  buf.sync_bw_ram();
  BookIndex::instance().build_index(menu->books_dir(), buf);
  BookIndex::instance().save(index_path);
  buf.reset_after_scratch(true);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

HomeScreen::Layout HomeScreen::layout_(const DrawBuffer& buf) const {
  const BitmapFont& hf = ListMenuScreen::shared_font(UiFont::Header);
  const BitmapFont& f = ListMenuScreen::shared_menu_font();
  const int W = buf.width();
  const int H = buf.height();
  const int line = f.y_advance();

  Layout L;
  L.header_h = kHeaderY + hf.y_advance() + kHeaderBottomGap;

  // Three covers side by side, in the 2:3 shape of most book covers.
  const int margin = std::max(12, W / 26);
  const int gap = std::max(10, W / 34);
  L.cover_w = (W - 2 * margin - 2 * gap) / 3;
  L.cover_h = L.cover_w * 3 / 2;

  const int link_h = line + 2;
  constexpr int kPercentGap = 10;  // cover → percent
  constexpr int kCaptionGap = 4;   // percent → title
  const int fixed = 2 * link_h + kPercentGap + line + kCaptionGap + line;
  constexpr int kEdgePad = 8;  // at least this much space under the header and above the bottom bar
  const int avail = H - L.header_h - kBottomAreaH - 2 * kEdgePad;
  constexpr int kMinGap = 16;
  const int frame_room = 2 * (kSelFrame + kSelGap);
  const int max_cover_h = avail - fixed - 2 * kMinGap - frame_room;
  if (L.cover_h > max_cover_h) {  // landscape: limited by height
    L.cover_h = std::max(30, max_cover_h);
    L.cover_w = L.cover_h * 2 / 3;
  }

  const int n = std::max(1, static_cast<int>(books_.size()));
  const int total_w = n * L.cover_w + (n - 1) * gap;
  for (int i = 0; i < kMaxBooks; ++i)
    L.slot_x[i] = (W - total_w) / 2 + i * (L.cover_w + gap);

  const int content = fixed + L.cover_h + frame_room;
  const int vgap = std::max(kMinGap, std::min(48, (avail - content) / 4));
  const int block = content + 2 * vgap;
  const int top = L.header_h + kEdgePad + std::max(0, (avail - block) / 2);

  L.finished_y = top;
  L.covers_y = L.finished_y + link_h + vgap + kSelFrame + kSelGap;
  L.percent_y = L.covers_y + L.cover_h + kPercentGap;
  L.caption_y = L.percent_y + line + kCaptionGap;
  L.library_y = L.caption_y + line + vgap + kSelFrame + kSelGap;
  return L;
}

// ---------------------------------------------------------------------------
// Cover thumbnails
// ---------------------------------------------------------------------------

bool HomeScreen::thumbs_missing_(const DrawBuffer& buf) const {
  if (!app_ || !app_->data_dir_)
    return false;
  const Layout L = layout_(buf);
  for (const RecentBook& b : books_)
    if (!file_exists(thumb_path(app_->data_dir_, b.path, L.cover_w, L.cover_h)))
      return true;
  return false;
}

void HomeScreen::make_thumbs_(DrawBuffer& buf) {
  if (!app_ || !app_->data_dir_)
    return;
  const Layout L = layout_(buf);

  // Decoding needs RAM: let go of the book index while it runs.
  BookIndex::instance().clear_entries();

  buf.sync_bw_ram();
  buf.show_loading("Loading covers...", 0);
  const int n = static_cast<int>(books_.size());
  for (int i = 0; i < n; ++i) {
    const RecentBook& b = books_[i];
    const std::string path = thumb_path(app_->data_dir_, b.path, L.cover_w, L.cover_h);
    if (file_exists(path))
      continue;
    make_dirs(path.substr(0, path.find_last_of('/')));

    bool made = false;
    {
      Book book;
      // Opening uses both drawing buffers as scratch; they're free afterwards.
      if (book.open(b.path.c_str(), buf.scratch_buf1(), buf.scratch_buf2(), /*parse_css_ncx=*/false) ==
          EpubError::Ok) {
        const int idx = sleep_screens::find_cover_entry(book.epub());
        if (idx >= 0) {
          const ZipEntry entry = book.epub().zip().entry(static_cast<size_t>(idx));
          buf.fill(true);
          int placed[4] = {0, 0, 0, 0};
          if (sleep_screens::draw_image_entry(buf, book.file(), entry, 0, 0, L.cover_w, L.cover_h,
                                              /*allow_rotate=*/false, buf.scratch_buf2(), DrawBuffer::kBufSize,
                                              placed, /*crop_pct=*/12) &&
              placed[2] > 0 && placed[3] > 0)
            made = write_thumb(buf, path, placed[0], placed[1], placed[2], placed[3]);
        }
      } else {
        MR_LOGI("home", "cover: cannot open %s", b.path.c_str());
      }
    }
    if (!made)
      write_empty_thumb(path);
    buf.show_loading("Loading covers...", (i + 1) * 100 / n);
  }
  buf.reset_after_scratch(true);

  // Take the index back (opening a book from here records it there).
  if (app_->data_dir_) {
    BookIndex::instance().load(std::string(app_->data_dir_) + "/book_index.dat");
    cached_generation_ = BookIndex::instance().generation();
  }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void HomeScreen::draw_cover_(DrawBuffer& buf, const Layout& L, int slot) const {
  const RecentBook& b = books_[slot];
  const BitmapFont& f = ListMenuScreen::shared_menu_font();
  const BitmapFont& small = ListMenuScreen::shared_font(UiFont::Small);
  const int x = L.slot_x[slot];
  const int cx = x + L.cover_w / 2;
  const int bottom = L.covers_y + L.cover_h;

  int rect[4] = {x, L.covers_y, L.cover_w, L.cover_h};
  const std::string thumb = app_ ? thumb_path(app_->data_dir_, b.path, L.cover_w, L.cover_h) : std::string();
  if (!thumb.empty() && draw_thumb(buf, thumb, cx, bottom, L.cover_w, L.cover_h, rect)) {
    frame(buf, rect[0] - 1, rect[1] - 1, rect[2] + 2, rect[3] + 2, 1);
  } else {
    // No cover: an outlined card with the title written in it.
    frame(buf, x, L.covers_y, L.cover_w, L.cover_h, 1);
    const int pad = 8;
    const int max_w = L.cover_w - 2 * pad;
    const int line = small.y_advance();
    const int max_lines = std::max(1, (L.cover_h - 2 * pad) / line);
    std::vector<std::string> lines;
    std::string cur;
    size_t pos = 0;
    const std::string& t = b.title;
    while (pos < t.size() && static_cast<int>(lines.size()) < max_lines) {
      size_t sp = t.find(' ', pos);
      if (sp == std::string::npos)
        sp = t.size();
      const std::string word = t.substr(pos, sp - pos);
      pos = sp + 1;
      const std::string trial = cur.empty() ? word : cur + " " + word;
      if (text_w(small, trial) <= max_w) {
        cur = trial;
      } else {
        if (!cur.empty())
          lines.push_back(cur);
        cur = word;
      }
    }
    if (!cur.empty() && static_cast<int>(lines.size()) < max_lines)
      lines.push_back(cur);
    if (pos < t.size() && !lines.empty())
      lines.back() += " ...";
    const int text_h = static_cast<int>(lines.size()) * line;
    int y = L.covers_y + (L.cover_h - text_h) / 2;
    for (const std::string& ln : lines) {
      const std::string shown = fit_text(small, ln, max_w);
      text_at(buf, small, cx - text_w(small, shown) / 2, y, shown);
      y += line;
    }
  }

  if (selected_ == slot + 1) {
    const int o = kSelGap + kSelFrame;
    frame(buf, rect[0] - o, rect[1] - o, rect[2] + 2 * o, rect[3] + 2 * o, kSelFrame);
  }

  // Completion under the cover.
  char pct[12];
  if (b.percent >= 0)
    std::snprintf(pct, sizeof(pct), "%d%%", std::min(100, b.percent));
  else
    std::snprintf(pct, sizeof(pct), "New");
  text_at(buf, f, cx - text_w(f, pct) / 2, L.percent_y, pct);
}

void HomeScreen::draw_(DrawBuffer& buf, std::optional<uint8_t> battery_pct) const {
  const BitmapFont& hf = ListMenuScreen::shared_font(UiFont::Header);
  const BitmapFont& f = ListMenuScreen::shared_menu_font();
  const int W = buf.width();
  const Layout L = layout_(buf);

  buf.fill(true);

  // Header.
  static const char kTitle[] = "Sami";
  text_at(buf, hf, (W - text_w(hf, kTitle)) / 2, kHeaderY, kTitle);

  // Finished Books link.
  char finished[40];
  std::snprintf(finished, sizeof(finished), "Finished Books (%u)",
                static_cast<unsigned>(app_ ? app_->finished_books().size() : 0));
  draw_link(buf, f, L.finished_y, finished, selected_ == 0);

  // The covers.
  if (books_.empty()) {
    static const char kLine1[] = "No books opened yet.";
    static const char kLine2[] = "Open one from the Library.";
    const int mid = L.covers_y + L.cover_h / 2;
    text_at(buf, f, (W - text_w(f, kLine1)) / 2, mid - f.y_advance(), kLine1);
    text_at(buf, f, (W - text_w(f, kLine2)) / 2, mid + 4, kLine2);
  } else {
    for (int i = 0; i < static_cast<int>(books_.size()); ++i)
      draw_cover_(buf, L, i);
    // The selected book's title, written out in full (covers are small).
    if (selected_ >= 1 && selected_ <= static_cast<int>(books_.size())) {
      const std::string caption = fit_text(f, books_[selected_ - 1].title, W - 32);
      text_at(buf, f, (W - text_w(f, caption)) / 2, L.caption_y, caption);
    }
  }

  // Library link.
  draw_link(buf, f, L.library_y, "Library", selected_ == library_index_());

  ListMenuScreen::draw_menu_bottom_bar(buf, f, app_ && app_->invert_menu_buttons(), battery_pct);
}

// ---------------------------------------------------------------------------
// Screen lifecycle
// ---------------------------------------------------------------------------

void HomeScreen::start(DrawBuffer& buf, IRuntime& runtime) {
  load_books_();
  const std::string first = books_.empty() ? std::string() : books_[0].path;
  switch (return_to_) {
    case Return::Finished:
      selected_ = 0;
      break;
    case Return::Library:
      // Back from the Library: stay on it, unless a book was opened there
      // (it's now the first cover).
      selected_ = (first == first_book_ || books_.empty()) ? library_index_() : 1;
      break;
    default:
      selected_ = books_.empty() ? library_index_() : 1;
      break;
  }
  return_to_ = Return::FirstBook;
  needs_thumbs_ = !needs_scan_ && thumbs_missing_(buf);
  draw_(buf, runtime.battery_percentage());
}

void HomeScreen::stop() {
  if (selected_ == 0)
    return_to_ = Return::Finished;
  else if (selected_ == library_index_())
    return_to_ = Return::Library;
  else
    return_to_ = Return::FirstBook;
  first_book_ = books_.empty() ? std::string() : books_[0].path;
  { std::vector<RecentBook> tmp; books_.swap(tmp); }
  BookIndex::instance().clear_entries();
}

void HomeScreen::open_selected_() {
  if (!app_)
    return;
  if (selected_ == 0) {
    app_->push_screen(ScreenId::Finished);
    return;
  }
  if (selected_ == library_index_()) {
    app_->push_screen(ScreenId::MainMenu);
    return;
  }
  const int i = selected_ - 1;
  if (i < 0 || i >= static_cast<int>(books_.size()))
    return;
  const std::string path = books_[i].path;
  // Only record the opening when the index is in memory (saving an empty
  // index would wipe it).
  if (!BookIndex::instance().entries().empty())
    app_->record_book_opened(path);
  app_->reader()->set_path(path.c_str());
  app_->push_screen(ScreenId::Reader);
}

void HomeScreen::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) {
  // Books added/removed/renamed while this page is up (serial transfer).
  if (!needs_scan_ && cached_generation_ != BookIndex::instance().generation() &&
      !BookIndex::instance().entries().empty()) {
    const int keep = selected_;
    load_books_();
    selected_ = std::min(keep, library_index_());
    needs_thumbs_ = thumbs_missing_(buf);
    draw_(buf, runtime.battery_percentage());
    buf.full_refresh();
  }

  if (needs_scan_) {
    scan_(buf);
    load_books_();
    needs_scan_ = false;
    selected_ = books_.empty() ? library_index_() : 1;
    needs_thumbs_ = thumbs_missing_(buf);
    if (!needs_thumbs_) {
      draw_(buf, runtime.battery_percentage());
      buf.full_refresh();
    }
  }

  if (needs_thumbs_) {
    needs_thumbs_ = false;
    make_thumbs_(buf);
    draw_(buf, runtime.battery_percentage());
    buf.full_refresh();
  }

  const int n = count_();
  const bool inv_menu = app_ && app_->invert_menu_buttons();
  const Button up_front = inv_menu ? Button::Button2 : Button::Button3;
  const Button down_front = inv_menu ? Button::Button3 : Button::Button2;

  bool moved = false;
  Button btn;
  while (buttons.next_press(btn)) {
    if (btn == up_front || btn == Button::Up) {
      selected_ = selected_ > 0 ? selected_ - 1 : n - 1;
      moved = true;
    } else if (btn == down_front || btn == Button::Down) {
      selected_ = selected_ < n - 1 ? selected_ + 1 : 0;
      moved = true;
    } else if (btn == Button::Button0) {
      if (app_)
        app_->push_screen(ScreenId::Settings);
      return;
    } else if (btn == Button::Button1) {
      open_selected_();
      if (app_ && app_->has_pending_transition())
        return;
    }
  }

  if (moved) {
    draw_(buf, runtime.battery_percentage());
    buf.refresh();
  }
}

}  // namespace microreader
