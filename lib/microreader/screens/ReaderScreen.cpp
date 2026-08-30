#include "ReaderScreen.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "../Application.h"
#include "../HeapLog.h"
#include "../display/ui_font_small.h"

#ifdef ESP_PLATFORM
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#else
#include <filesystem>
#endif

namespace microreader {

// ---------------------------------------------------------------------------
// ReaderScreen — path helpers
// ---------------------------------------------------------------------------

std::string ReaderScreen::book_stem_() const {
  const char* name = path_.c_str();
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
  return std::string(name, len);
}

// ---------------------------------------------------------------------------
// ReaderScreen — image size resolution
// ---------------------------------------------------------------------------

// resolve_image_size_ removed — image size resolution is now handled by
// make_image_size_query() (MrbReader.h), stored in image_size_fn_.

bool ReaderScreen::decode_image_to_buffer_(uint16_t img_key, uint32_t offset, DrawBuffer& buf, int dest_x, int dest_y,
                                           uint16_t max_w, uint16_t max_h, uint16_t src_y, uint16_t clip_h) {
  char cache_path[256];
  snprintf(cache_path, sizeof(cache_path), "%s/img_%u_%ux%u.bin", book_cache_dir_.c_str(),
           static_cast<unsigned>(img_key), static_cast<unsigned>(max_w), static_cast<unsigned>(max_h));

  FILE* cache_f = std::fopen(cache_path, "rb");
  if (cache_f) {
    uint16_t header[2] = {0, 0};
    if (std::fread(header, 2, 2, cache_f) == 2) {
      uint16_t cached_w = header[0];
      uint16_t cached_h = header[1];
      uint16_t row_bytes = (cached_w + 7) / 8;
      std::vector<uint8_t> row_buf(row_bytes);
      for (uint16_t r = 0; r < cached_h; ++r) {
        if (std::fread(row_buf.data(), 1, row_bytes, cache_f) != row_bytes)
          break;
        if (r < src_y)
          continue;
        uint16_t dest_row = static_cast<uint16_t>(r - src_y);
        if (clip_h > 0 && dest_row >= clip_h)
          break;
        buf.blit_1bit_row(dest_x, dest_y + dest_row, row_buf.data(), cached_w);
      }
      std::fclose(cache_f);
      return true;
    }
    std::fclose(cache_f);
  }

  StdioZipFile file;
  if (!file.open(path_.c_str()))
    return false;
  ZipEntry entry;
  if (ZipReader::read_local_entry(file, offset, entry) != ZipError::Ok)
    return false;

  FILE* cache_w = std::fopen(cache_path, "wb");
  if (cache_w) {
    uint16_t dummy[2] = {0, 0};
    std::fwrite(dummy, 2, 2, cache_w);
  }

  // Set up a sink that blits each dithered row directly to the DrawBuffer.
  struct BlitCtx {
    DrawBuffer* buf;
    int x, y;
    uint16_t src_y;
    uint16_t clip_h;  // max rows to render (0 = no clip)
    FILE* cache_w;
    uint16_t out_w;
    uint16_t out_h;
  };
  BlitCtx ctx{&buf, dest_x, dest_y, src_y, clip_h, cache_w, 0, 0};
  ImageRowSink sink;
  sink.ctx = &ctx;
  sink.emit_row = [](void* c, uint16_t row, const uint8_t* data, uint16_t width) {
    auto* bc = static_cast<BlitCtx*>(c);
    bc->out_w = width;
    if (row >= bc->out_h)
      bc->out_h = static_cast<uint16_t>(row + 1);

    if (bc->cache_w) {
      uint16_t row_bytes = static_cast<uint16_t>((width + 7) / 8);
      std::fwrite(data, 1, row_bytes, bc->cache_w);
    }

    if (row < bc->src_y)
      return;
    uint16_t dest_row = static_cast<uint16_t>(row - bc->src_y);
    if (bc->clip_h > 0 && dest_row >= bc->clip_h)
      return;
    bc->buf->blit_1bit_row(bc->x, bc->y + dest_row, data, width);
  };

  // Pixel sink for Adam7 interlaced PNGs: writes pixels directly to the
  // DrawBuffer with no intermediate output buffer.  Caching is skipped for
  // Adam7 (pixels arrive out of order so we can't write a sequential cache).
  struct PixelCtx {
    DrawBuffer* buf;
    int x, y;
    uint16_t src_y;
    uint16_t clip_h;
    uint16_t out_w;
    uint16_t out_h;
  };
  PixelCtx pctx{&buf, dest_x, dest_y, src_y, clip_h, 0, 0};
  ImagePixelSink psink;
  psink.ctx = &pctx;
  psink.set_pixel = [](void* c, uint16_t px, uint16_t py, bool white) {
    auto* pc = static_cast<PixelCtx*>(c);
    if (px >= pc->out_w)
      pc->out_w = static_cast<uint16_t>(px + 1);
    if (py >= pc->out_h)
      pc->out_h = static_cast<uint16_t>(py + 1);
    if (py < pc->src_y)
      return;
    uint16_t dest_row = static_cast<uint16_t>(py - pc->src_y);
    if (pc->clip_h > 0 && dest_row >= pc->clip_h)
      return;
    pc->buf->set_pixel(pc->x + static_cast<int>(px), pc->y + static_cast<int>(dest_row), white);
  };

  // Use the active display buffer as the work buffer to avoid a 44KB heap
  // allocation.  The active buffer is safe to overwrite here: it is not
  // needed for this render pass and will be cleared before the next refresh.
  DecodedImage dims;  // only width/height will be set; data stays empty
  auto err = decode_image_from_entry(file, entry, max_w, max_h, dims, buf.scratch_buf2(), DrawBuffer::kBufSize,
                                     /*scale_to_fill=*/true, &sink, &psink);

  if (cache_w) {
    if (err == ImageError::Ok && ctx.out_w > 0 && ctx.out_h > 0) {
      std::fseek(cache_w, 0, SEEK_SET);
      uint16_t header[2] = {ctx.out_w, ctx.out_h};
      std::fwrite(header, 2, 2, cache_w);
    }
    std::fclose(cache_w);
    // Delete cache file if decode failed, or if Adam7 pixel_sink was used
    // (pixels written directly to DrawBuffer, no sequential cache data written).
    if (err != ImageError::Ok || ctx.out_w == 0) {
#ifdef ESP_PLATFORM
      unlink(cache_path);
#else
      std::remove(cache_path);
#endif
    }
  }

  return err == ImageError::Ok;
}

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Bookmark / key-file helpers (must precede start())
// ---------------------------------------------------------------------------

// FNV-1a 32-bit hash of an arbitrary byte string.
static uint32_t fnv1a_32(const std::string& s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s)
    h = (h ^ c) * 16777619u;
  return h;
}

// Build a stable, filesystem-safe book key from raw metadata.
// Always produces an 8-char lowercase hex string derived from a hash of
// title + author + language — works for any language/script.
static std::string make_book_key(const EpubMetadata& meta) {
  std::string raw = meta.title;
  if (meta.author && !meta.author->empty()) {
    raw += '|';
    raw += *meta.author;
  }
  if (meta.language && !meta.language->empty()) {
    raw += '|';
    raw += *meta.language;
  }
  char hex[9];
  snprintf(hex, sizeof(hex), "%08lx", static_cast<unsigned long>(fnv1a_32(raw)));
  return std::string(hex);
}

// Build the legacy slug key used by older versions (ASCII-only sanitization).
// Used only when loading, to migrate existing .pos files to the new key.
static std::string make_book_key_legacy(const EpubMetadata& meta, const char* epub_path) {
  auto sanitize = [](const std::string& s) {
    std::string out;
    bool last_dash = false;
    for (unsigned char c : s) {
      if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
        out += static_cast<char>(c);
        last_dash = false;
      } else if (c >= 'A' && c <= 'Z') {
        out += static_cast<char>(c + 32);
        last_dash = false;
      } else if (!last_dash) {
        out += '-';
        last_dash = true;
      }
    }
    while (!out.empty() && out.back() == '-')
      out.pop_back();
    while (!out.empty() && out.front() == '-')
      out.erase(out.begin());
    return out;
  };

  if (!meta.title.empty()) {
    std::string title_part = sanitize(meta.title);
    if (!title_part.empty()) {
      std::string key = title_part;
      if (meta.author && !meta.author->empty()) {
        std::string a = sanitize(*meta.author);
        if (!a.empty()) {
          key += '-';
          key += a;
        }
      }
      if (meta.language && !meta.language->empty()) {
        std::string l = sanitize(*meta.language);
        if (!l.empty()) {
          key += '-';
          key += l;
        }
      }
      if (key.size() > 80)
        key.resize(80);
      return key;
    }
  }
  // Fallback: epub basename.
  const char* name = epub_path;
  const char* sep = std::strrchr(epub_path, '/');
#ifdef _WIN32
  const char* bsep = std::strrchr(epub_path, '\\');
  if (bsep && (!sep || bsep > sep))
    sep = bsep;
#endif
  if (sep)
    name = sep + 1;
  const char* dot = std::strrchr(name, '.');
  size_t len = dot ? static_cast<size_t>(dot - name) : std::strlen(name);
  return std::string(name, len);
}

void ReaderScreen::start(DrawBuffer& buf, IRuntime& runtime) {
  buf_ = &buf;
  book_key_.clear();
  pos_path_.clear();
  MR_LOGI("reader", "start: path='%s'", path_.c_str());

  if (app_ && app_->font_manager())
    app_->font_manager()->ensure_ready(buf);
  MR_LOGI("reader", "font ready");

  // Build cache path: <data_dir>/cache/<stem>/book.mrb
  book_cache_dir_ = data_dir_ + "/cache/" + book_stem_();
#ifdef ESP_PLATFORM
  mkdir(book_cache_dir_.c_str(), 0775);
#else
  std::filesystem::create_directories(book_cache_dir_);
#endif
  mrb_path_ = book_cache_dir_ + "/book.mrb";

  MR_LOGI("reader", "mrb_path='%s'", mrb_path_.c_str());
  bool mrb_ok = mrb_.open(mrb_path_.c_str());
  MR_LOGI("reader", "mrb_ok=%d", (int)mrb_ok);

  const bool cache_only = cache_only_;
  cache_only_ = false;
  if (!mrb_ok && cache_only) {
    MR_LOGI("reader", "cache-only open: MRB missing — returning to book list");
    open_ok_ = false;
    if (app_)
      app_->pop_screen();
    return;
  }
  buf_was_touched_ = false;

  if (!mrb_ok) {
    // Upload the current frame before scratch buffer use so the display
    // controller has a valid reference frame for partial refreshes.
    MR_LOGI("reader", "mrb miss — opening epub: '%s'", path_.c_str());
    {
      FILE* check = std::fopen(path_.c_str(), "r");
      if (!check) {
        MR_LOGI("reader", "epub not found: '%s'", path_.c_str());
        open_ok_ = false;
        goto show_error;
      }
      std::fclose(check);
    }
    buf_was_touched_ = true;
    buf.sync_bw_ram();
    buf.show_loading("Converting...", 0);

#ifdef ESP_PLATFORM
    int64_t open_start = esp_timer_get_time();
#endif
    auto err = book_.open(path_.c_str(), buf.scratch_buf1(), buf.scratch_buf2());
#ifdef ESP_PLATFORM
    long open_ms = (long)((esp_timer_get_time() - open_start) / 1000);
    ESP_LOGI("perf", "Book::open: %ldms", open_ms);
#endif
    if (err != EpubError::Ok || book_.chapter_count() == 0) {
      MR_LOGI("reader", "epub open failed: err=%d chapters=%u", (int)err, (unsigned)book_.chapter_count());
      open_ok_ = false;
      goto show_error;
    }

#ifdef ESP_PLATFORM
    int64_t conv_start = esp_timer_get_time();
#endif
    if (!convert_epub_to_mrb_streaming(book_, mrb_path_.c_str(), buf.scratch_buf1(), buf.scratch_buf2(),
                                       [&buf](int done, int total) {
                                         int pct = total > 0 ? (done * 100 / total) : 0;
                                         buf.show_loading("Converting...", pct);
                                       })) {
      MR_LOGI("reader", "mrb conversion failed");
      open_ok_ = false;
      goto show_error;
    }
#ifdef ESP_PLATFORM
    long conv_ms = (long)((esp_timer_get_time() - conv_start) / 1000);
    long total_ms = (long)((esp_timer_get_time() - open_start) / 1000);
    ESP_LOGI("perf", "Conversion: %ldms  (open+convert=%ldms)", conv_ms, total_ms);
#endif

    // TODO: Maybe handle different aspect ratios. Possible optios are stretch
    // (what it does now), crop to fit, or show empty borders. Configuration option?

    // Width and height are flipped to decode the cover for portrait.
    DecodedImage decoded_cover;
    book_.decode_image(book_.cover_entry_index(), decoded_cover, DisplayFrame::kPhysicalHeight,
                       DisplayFrame::kPhysicalWidth, buf.scratch_buf1(), DrawBuffer::kBufSize);

    std::string cover_cache_path = book_cache_dir_ + "/cover.mgr";
    FILE* cover_cache_file = std::fopen(cover_cache_path.c_str(), "wb");

    std::fwrite("MGR2", 1, 4, cover_cache_file);
    std::fwrite(&decoded_cover.height, 1, 2, cover_cache_file);
    std::fwrite(&decoded_cover.width, 1, 2, cover_cache_file);

    // MGR format expects 2 bit grayscale color but we have 1 bit monochrome. Write
    // each bit twice to get the effect.
    // HACK: This may be slow, benchmark it. Possible optimization opportunities:
    // buffered writes, better iteration.

    uint8_t* src = decoded_cover.data.data();

    uint8_t running_byte = 0;
    uint8_t bits_writen_to_byte = 0;
    for (int out_y = 0; out_y < DisplayFrame::kPhysicalHeight; out_y++) {
      for (int out_x = 0; out_x < DisplayFrame::kPhysicalWidth; out_x++) {
        int src_x = decoded_cover.width - 1 - out_y;
        int src_y = out_x;

        int src_i = src_y * decoded_cover.width + src_x;

        uint8_t bit = (src[src_i / 8] >> (7 - (src_i % 8))) & 1;

        // NOTE: Why does this need to be inverted.
        bit = !bit;

        uint8_t bit_twice = bit << 1 | bit;

        running_byte <<= 2;
        running_byte |= bit_twice;

        bits_writen_to_byte += 2;
        if (bits_writen_to_byte == 8) {
          std::fwrite(&running_byte, 1, 1, cover_cache_file);
          bits_writen_to_byte = 0;
        }
      }

      if (bits_writen_to_byte > 0) {
        std::fwrite(&running_byte, 1, 1, cover_cache_file);
        bits_writen_to_byte = 0;
      }
    }

    std::fclose(cover_cache_file);

    book_.close();

    // Reset both display buffers to white after scratch use (conversion
    // corrupted them). render_page_ will fill the inactive buffer fresh.
    buf.reset_after_scratch(true);

    mrb_ok = mrb_.open(mrb_path_.c_str());
    if (!mrb_ok) {
      MR_LOGI("reader", "mrb open failed after conversion");
      open_ok_ = false;
      goto show_error;
    }
  }

  // Derive a stable book key from MRB metadata (title + author).
  // This survives epub file renames while staying unique across different books.
  book_key_ = make_book_key(mrb_.metadata());
  pos_path_ = std::string(data_dir_) + "/data/" + book_key_ + ".pos";

  open_ok_ = true;
  chapter_idx_ = 0;
  page_pos_ = PagePosition{0, 0};
  image_size_fn_ = make_image_size_query(mrb_, path_, static_cast<uint16_t>(buf.width()));
  saved_chapter_idx_ = 0;
  saved_page_pos_ = PagePosition{0, 0};
  load_position_();
  load_chapter_(saved_chapter_idx_);
  if (!chapter_src_) {
    // Fallback to chapter 0 if saved index is invalid.
    saved_chapter_idx_ = 0;
    saved_page_pos_ = PagePosition{0, 0};
    load_chapter_(0);
  }
  if (!chapter_src_) {
    open_ok_ = false;
    goto show_error;
  }
  page_pos_ = saved_page_pos_;
  layout_engine_ = TextLayout{};
  layout_engine_.set_source(*chapter_src_);
  layout_engine_.set_image_size_fn(image_size_fn_);
  layout_engine_.set_hyphenation_lang(detect_language(mrb_.metadata().language));
  render_page_(buf);
#ifdef ESP_PLATFORM
  ESP_LOGI("reader", "BOOK_OK: %s", path_.c_str());
#endif
  return;

show_error:
#ifdef ESP_PLATFORM
  ESP_LOGE("reader", "BOOK_FAIL: %s", path_.c_str());
#endif
  if (buf_was_touched_) {
    buf.fill(true);
    buf.draw_text(kPaddingLeft, kPaddingTop, "Failed to open book", true, kScale);
  }
}

void ReaderScreen::resume(DrawBuffer& buf, IRuntime& runtime) {
  buf_ = &buf;
  if (!open_ok_)
    return;

  // Handle pending chapter jump (from ChapterSelectScreen).
  if (app_ && app_->chapter_select()->has_pending()) {
    saved_chapter_idx_ = app_->chapter_select()->pending_chapter();
    saved_page_pos_ = PagePosition{app_->chapter_select()->pending_para_index(), 0, 0};
    app_->chapter_select()->clear_pending();
    load_chapter_(saved_chapter_idx_);
    page_pos_ = saved_page_pos_;
    layout_engine_.set_source(*chapter_src_);
    layout_engine_.set_image_size_fn(image_size_fn_);
    layout_engine_.set_hyphenation_lang(detect_language(mrb_.metadata().language));
  } else if (app_ && app_->links_screen()->has_pending()) {
    if (nav_history_.size() < kMaxNavHistory)
      nav_history_.push_back({saved_chapter_idx_, saved_page_pos_});
    saved_chapter_idx_ = app_->links_screen()->pending_chapter();
    saved_page_pos_ = PagePosition{app_->links_screen()->pending_para(), 0, 0};
    app_->links_screen()->clear_pending();
    load_chapter_(saved_chapter_idx_);
    page_pos_ = saved_page_pos_;
    layout_engine_.set_source(*chapter_src_);
    layout_engine_.set_image_size_fn(image_size_fn_);
    layout_engine_.set_hyphenation_lang(detect_language(mrb_.metadata().language));
  }
  // Check if font settings changed (font_size_idx may have been updated in options).
  if (const BitmapFontSet* fset = ext_font_set_ ? ext_font_set_ : (font_set_.valid() ? &font_set_ : nullptr)) {
    const_cast<BitmapFontSet*>(fset)->set_base_size_index(reader_settings_.font_size_idx);
  }

  // Make sure to apply settings when coming from the reader options screen
  if (chapter_src_)
    layout_engine_.set_source(*chapter_src_);

  render_page_(buf);
  save_position_();
}

void ReaderScreen::stop() {
  image_size_fn_ = {};
  chapter_src_.reset();
  mrb_.close();
  book_.close();
  if (open_ok_)
    save_position_();
  page_ = PageContent{};
  mrb_path_.clear();
  mrb_path_.shrink_to_fit();
  pos_path_.clear();
  pos_path_.shrink_to_fit();
  book_key_.clear();
  book_key_.shrink_to_fit();
  nav_history_.clear();
  open_ok_ = false;
  // saved_chapter_idx_ / saved_page_pos_ are intentionally NOT reset here —
  // resume() uses them as the nav-history origin when a link jump is pending.
  buf_ = nullptr;
}

void ReaderScreen::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& /*runtime*/) {
  if (!open_ok_) {
    // Auto-pop if the book was never found (no display was touched).
    // Otherwise wait for back button so user can see the error message.
    if (!buf_was_touched_) {
      app_->pop_screen();
      return;
    }
    // Still drain the history so stale events don't bleed into the next frame.
    Button btn;
    while (buttons.next_press(btn)) {
      if (btn == Button::Button0) {
        app_->pop_screen();
        return;
      }
    }
    return;
  }

  // Process press events in the order they arrived.
  int page_delta = 0;
  bool had_next_press = false;
  bool had_prev_press = false;

  bool inv_side = app_ && app_->invert_side_buttons();
  bool inv_bottom = app_ && app_->invert_bottom_paging();

  // Default: Button3=next, Button2=prev (inv_bottom=true flips front buttons).
  // Default: Up=next, Down=prev (inv_side=false keeps top=next).
  Button logical_next_front = inv_bottom ? Button::Button2 : Button::Button3;
  Button logical_prev_front = inv_bottom ? Button::Button3 : Button::Button2;
  Button logical_next_side = inv_side ? Button::Down : Button::Up;
  Button logical_prev_side = inv_side ? Button::Up : Button::Down;

  Button btn;
  while (buttons.next_press(btn)) {
    if (btn == logical_next_front || btn == logical_next_side) {
      ++page_delta;
      had_next_press = true;
    } else if (btn == logical_prev_front || btn == logical_prev_side) {
      --page_delta;
      had_prev_press = true;
    } else {
      switch (btn) {
        case Button::Button0:
          if (!nav_history_.empty()) {
            // Navigate back to the previous position in the history stack.
            NavHistoryEntry origin = nav_history_.back();
            nav_history_.pop_back();
            MR_LOGI("nav", "back -> ch=%u para=%u (remaining history=%u)", (unsigned)origin.chapter_idx,
                    (unsigned)origin.page_pos.paragraph, (unsigned)nav_history_.size());
            load_chapter_(origin.chapter_idx);
            page_pos_ = origin.page_pos;
            layout_engine_.set_source(*chapter_src_);
            layout_engine_.set_image_size_fn(image_size_fn_);
            layout_engine_.set_hyphenation_lang(detect_language(mrb_.metadata().language));
            render_page_(buf);
            buf.refresh();
            save_position_();
            return;
          }
          app_->pop_screen();
          return;
        case Button::Button1:
          if (!nav_history_.empty()) {
            // Stay here: clear the nav history and redraw to remove hints.
            nav_history_.clear();
            render_page_(buf);
            buf.refresh();
            return;
          }
          saved_chapter_idx_ = chapter_idx_;
          saved_page_pos_ = page_pos_;
          app_->reader_options()->set_settings(&reader_settings_);
          app_->reader_options()->populate(mrb_.toc(), static_cast<uint16_t>(chapter_idx_), page_pos_.paragraph,
                                           mrb_.metadata().title, progress_pct(), chapter_progress_pct());
          app_->reader_options()->set_page_links(page_links_, mrb_.spine_files(), mrb_);
          app_->push_screen(ScreenId::ReaderOptions);
          return;
        default:
          break;
      }
    }
  }

  // Hold-down: advance one page per frame while a nav button is held,
  // but only if no fresh press event arrived this frame (avoids double-counting
  // the initial press).
  if (!had_next_press && (buttons.is_down(logical_next_front) || buttons.is_down(logical_next_side)))
    ++page_delta;
  if (!had_prev_press && (buttons.is_down(logical_prev_front) || buttons.is_down(logical_prev_side)))
    --page_delta;

  bool changed = false;
  if (page_delta > 0) {
    for (int i = 0; i < page_delta; ++i)
      changed = next_page_() || changed;
  } else if (page_delta < 0) {
    for (int i = 0; i > page_delta; --i)
      changed = prev_page_() || changed;
  }

  if (changed) {
    if (grayscale_active_) {
      buf.revert_grayscale();
      grayscale_active_ = false;
    }
    render_page_(buf);
    buf.refresh();
    save_position_();
  }

  // Deferred grayscale: only apply when no nav buttons are held, so rapid
  // page flipping stays fast and grayscale is applied once the user stops.
  if (grayscale_pending_ && !buttons.is_down(Button::Button2) && !buttons.is_down(Button::Button3) &&
      !buttons.is_down(Button::Up) && !buttons.is_down(Button::Down)) {
    grayscale_pending_ = false;
    apply_grayscale_(buf);
    grayscale_active_ = true;
  }
}

void ReaderScreen::load_chapter_(size_t idx) {
  chapter_src_.reset();
  if (idx < mrb_.chapter_count()) {
    chapter_src_ = std::make_unique<MrbChapterSource>(mrb_, static_cast<uint16_t>(idx));
    chapter_idx_ = idx;
    layout_engine_.set_source(*chapter_src_);
  }
}

void ReaderScreen::collect_page_links_() {
  page_links_.clear();
  if (!chapter_src_)
    return;

  // Collect the unique hrefs that appear in rendered layout words.
  // Using the layout words (not raw runs) means we only see links from lines
  // actually on this page — no off-screen content from paragraphs that span
  // a page boundary.
  static constexpr int kMaxLinks = 32;
  const char* seen_hrefs[kMaxLinks];
  uint16_t seen_para[kMaxLinks];
  int seen_count = 0;
  for (const auto& ci : page_.items) {
    const PageTextItem* item = std::get_if<PageTextItem>(&ci);
    if (!item)
      continue;
    for (const auto& w : item->line.words) {
      if (!w.href)
        continue;
      bool already = false;
      for (int k = 0; k < seen_count; ++k)
        if (seen_hrefs[k] == w.href) {
          already = true;
          break;
        }
      if (!already && seen_count < kMaxLinks) {
        seen_hrefs[seen_count] = w.href;
        seen_para[seen_count] = item->paragraph_index;
        ++seen_count;
      }
    }
  }

  // Now build labels from the source runs so the text is never split by
  // hyphenation or line-wrap — but only for hrefs we confirmed are on screen.
  static constexpr size_t kMaxLabel = 64;
  for (int k = 0; k < seen_count; ++k) {
    const char* href_ptr = seen_hrefs[k];
    const Paragraph& para = chapter_src_->paragraph(seen_para[k]);
    if (para.type != ParagraphType::Text)
      continue;
    std::string label;
    std::string href_str;
    for (const auto& run : para.text.runs) {
      if (run.href.empty() || run.href.c_str() != href_ptr)
        continue;
      if (href_str.empty())
        href_str = run.href;
      if (label.size() < kMaxLabel) {
        if (!label.empty() && label.back() != ' ')
          label += ' ';
        const size_t room = kMaxLabel - label.size();
        label.append(run.text, 0, room);
      }
    }
    if (!href_str.empty())
      page_links_.push_back({std::move(label), std::move(href_str)});
  }
}

void ReaderScreen::render_page_(DrawBuffer& buf) {
  const int W = buf.width();
  const int H = buf.height();

#ifdef ESP_PLATFORM
  int64_t t0 = esp_timer_get_time();
#endif

  // Use proportional font if available, otherwise fallback to fixed.
  FixedFont fixed_font(kGlyphW * kScale, kGlyphH * kScale + 4);
  const BitmapFontSet* fset = ext_font_set_ ? ext_font_set_ : (font_set_.valid() ? &font_set_ : nullptr);
  if (fset) {
    const_cast<BitmapFontSet*>(fset)->set_base_size_index(reader_settings_.font_size_idx);
  }
  IFont& font = fset ? static_cast<IFont&>(const_cast<BitmapFontSet&>(*fset)) : static_cast<IFont&>(fixed_font);
  std::optional<Alignment> align_override = std::nullopt;
  if (reader_settings_.align_override != AlignOverride::Book) {
    if (reader_settings_.align_override == AlignOverride::Left)
      align_override = Alignment::Start;
    else if (reader_settings_.align_override == AlignOverride::Center)
      align_override = Alignment::Center;
    else if (reader_settings_.align_override == AlignOverride::Right)
      align_override = Alignment::End;
    else if (reader_settings_.align_override == AlignOverride::Justify)
      align_override = Alignment::Justify;
  }

  const bool landscape = buf.rotation() == Rotation::Deg0;
  PageOptions opts = make_page_opts(&reader_settings_, W, H);
  opts.align_override = align_override;
  opts.padding_right = reader_settings_.h_padding();
  if (landscape && !nav_history_.empty()) {
    // Hints are drawn in portrait coords; portrait Y=pct_top..800 maps to landscape X=pct_top..800.
    // Ensure padding_right clears that strip so text doesn't overlap the hints.
    const uint16_t hint_strip = (reader_settings_.progress_style == ProgressStyle::Bar) ? 18u : 16u;
    opts.padding_right = std::max(opts.padding_right, hint_strip);
  }
  opts.padding_bottom = bottom_padding_(landscape);
  opts.padding_left = reader_settings_.h_padding();
  opts.padding_top = static_cast<uint16_t>(kPaddingTop + reader_settings_.v_padding());
  opts.line_height_multiplier_percent = reader_settings_.line_height_multiplier_percent();
  opts.center_text = true;
  opts.override_publisher_fonts = reader_settings_.override_publisher_fonts;
  layout_engine_.set_font(font);
  layout_engine_.set_options(opts);
  page_pos_ = layout_engine_.resolve_stable_position(page_pos_);
  layout_engine_.set_position(page_pos_);

#ifdef ESP_PLATFORM
  int64_t t_layout = esp_timer_get_time();
#endif
  page_ = layout_engine_.layout();
  collect_page_links_();
#ifdef ESP_PLATFORM
  long layout_us = (long)(esp_timer_get_time() - t_layout);
#endif
  // ─────────────────────────────────
  struct ImageToDraw {
    uint16_t key;
    int x, y, w, h;
    uint32_t offset;
    uint16_t src_y = 0;
    uint16_t clip_h = 0;  // rendered slice height (0 = full)
  };
  std::vector<ImageToDraw> images;
  auto collect_img = [&](const PageImageItem& img_item) {
    const int img_w = static_cast<int>(img_item.width);
    const int img_h = static_cast<int>(img_item.height);
    if (img_w <= 0 || img_h <= 0)
      return;
    if (img_item.key >= mrb_.image_count())
      return;
    ImageToDraw itd;
    itd.key = img_item.key;
    itd.x = static_cast<int>(img_item.x_offset);
    itd.y = static_cast<int>(img_item.y_offset);  // y_offset is absolute (vertical centering baked in)
    itd.w = img_w;
    // Use full_height as max_h so the decoder scales to the correct aspect ratio;
    // src_y crops to the visible slice within that full render.
    itd.h = img_item.full_height > 0 ? static_cast<int>(img_item.full_height) : img_h;
    itd.offset = mrb_.image_ref(img_item.key).local_header_offset;
    itd.src_y = img_item.y_crop;
    // Clip rendered rows to the slice height so the image doesn't overflow past
    // its layout-assigned area (e.g. into the page number zone or page N+1).
    itd.clip_h = static_cast<uint16_t>(img_h);
    images.push_back(itd);
  };
  for (const auto& ci : page_.items) {
    if (const PageImageItem* img = std::get_if<PageImageItem>(&ci)) {
      collect_img(*img);
    } else if (const PageTextItem* ti = std::get_if<PageTextItem>(&ci)) {
      if (ti->inline_image.has_value())
        collect_img(*ti->inline_image);
    }
  }

  // Track whether grayscale pass is needed (deferred to update()).
  grayscale_pending_ = fset && fset->has_grayscale();

  // ── BW rendering
  // ────────────────────────────────────────────────────────
#ifdef ESP_PLATFORM
  int64_t t_draw = esp_timer_get_time();
#endif
  buf.fill(true);

  if (fset) {
    render_text_(buf, *fset, GrayPlane::BW, false, reader_settings_.h_padding());
  } else {
    for (const auto& ci : page_.items) {
      const PageTextItem* item = std::get_if<PageTextItem>(&ci);
      if (!item)
        continue;
      for (const auto& w : item->line.words) {
        if (w.len == 0)
          continue;
        char text[64];
        int tlen = static_cast<int>(w.len);
        if (tlen > 63)
          tlen = 63;
        std::memcpy(text, w.text, tlen);
        text[tlen] = '\0';
        buf.draw_text_no_bg(reader_settings_.h_padding() + w.x, static_cast<int>(item->y_offset), text, false /*black*/,
                            kScale);
      }
    }
  }

  for (const auto& hr : page_.items) {
    const PageHrItem* h = std::get_if<PageHrItem>(&hr);
    if (!h)
      continue;
    const int hr_y = static_cast<int>(h->y_offset) + static_cast<int>(h->height) / 2;
    buf.fill_rect(static_cast<int>(h->x_offset), hr_y - 1, static_cast<int>(h->width), 2, false);
  }

  for (const auto& itd : images) {
    if (!decode_image_to_buffer_(itd.key, itd.offset, buf, itd.x, itd.y, static_cast<uint16_t>(itd.w),
                                 static_cast<uint16_t>(itd.h), itd.src_y, itd.clip_h)) {
      buf.fill_rect(itd.x, itd.y, itd.w, itd.h, false);
    }
  }

  draw_bottom_(buf, landscape);

  // ── Timing
  // ──────────────────────────────────────────────────────────────
  int n_words = 0;
  for (const auto& ci : page_.items)
    if (const PageTextItem* ti = std::get_if<PageTextItem>(&ci))
      n_words += static_cast<int>(ti->line.words.size());

#ifdef ESP_PLATFORM
  long render_us = (long)(esp_timer_get_time() - t0);
  long draw_us = (long)(esp_timer_get_time() - t_draw);
  ESP_LOGI("perf",
           "render_page: %ldms total (layout=%ldms[miss=%d para=%ldms hyph=%ldms metrics=%ldms] draw=%ldms) words=%d "
           "images=%d",
           render_us / 1000, layout_us / 1000, g_layout_cache_misses, (long)(g_layout_para_us / 1000),
           (long)(g_layout_hyph_us / 1000), (long)(g_layout_metrics_us / 1000), draw_us / 1000, n_words,
           (int)images.size());
#endif
}

uint16_t ReaderScreen::bottom_padding_(bool landscape) const {
  const uint16_t base =
      static_cast<uint16_t>(reader_settings_.progress_bottom() + reader_settings_.v_padding() + (landscape ? 2 : 0));
  if (nav_history_.empty() || landscape)
    return base;  // landscape hints go on the right edge, not bottom — no extra bottom padding needed
  // Portrait: hints share the bottom margin. Need at least as much room as the percentage indicator, plus 2px for bar.
  const uint16_t pct_base = static_cast<uint16_t>(16 + reader_settings_.v_padding() + 2);
  if (reader_settings_.progress_style == ProgressStyle::Bar)
    return std::max(base, static_cast<uint16_t>(pct_base + 2));
  if (reader_settings_.progress_style == ProgressStyle::None)
    return std::max(base, pct_base);
  return base;  // Percentage already reserves enough
}

void ReaderScreen::draw_bottom_(DrawBuffer& buf, bool landscape) {
  const int W = buf.width();
  const int H = buf.height();

  if (mrb_.paragraph_count() > 0 && reader_settings_.progress_style != ProgressStyle::None) {
    int pct = reader_settings_.progress_scope == ProgressScope::Chapter ? chapter_progress_pct() : progress_pct();
    if (reader_settings_.progress_style == ProgressStyle::Percentage) {
      char pct_str[8];
      snprintf(pct_str, sizeof(pct_str), "%d%%", pct);
      buf.draw_text_centered(W / 2, landscape ? H - 18 : H - 16, pct_str, true);
    } else {
      // Progress bar: a thin filled line at the very bottom of the screen.
      // Move it up 2px in landscape mode because of the screen edges being partially hidden.
      const int kBarY = landscape ? H - 4 : H - 2;
      constexpr int kBarH = 2;
      const int bar_w = pct * W / 100;
      buf.fill_rect(0, kBarY, bar_w, kBarH, false);         // filled portion (black)
      buf.fill_rect(bar_w, kBarY, W - bar_w, kBarH, true);  // unfilled portion (white)
    }
  }

  if (nav_history_.empty())
    return;

  if (!hint_font_.valid())
    hint_font_.init(kFontData_ui_small_mbf, kFontData_ui_small_mbf_size);
  if (!hint_font_.valid())
    return;

  // Always draw hints in portrait (Deg90) coordinates so they land at the same
  // physical button locations regardless of current rotation.
  const Rotation saved_rotation = buf.rotation();
  buf.set_rotation_transform(Rotation::Deg90);
  const int W90 = buf.width();
  const int H90 = buf.height();

  const int pair0 = W90 * 163 / 550;
  const int gap = 50;
  const int btn0_pos = pair0 - gap;  // Button0 = back
  const int btn1_pos = pair0 + gap;  // Button1 = stay
  const int pct_top = (reader_settings_.progress_style == ProgressStyle::Bar) ? H90 - 18 : H90 - 16;
  const int text_y = pct_top + static_cast<int>(hint_font_.baseline());

  const char* kBack = "back";
  const char* kStay = "stay";
  const size_t back_len = std::strlen(kBack);
  const size_t stay_len = std::strlen(kStay);
  const int bw = hint_font_.word_width(kBack, back_len, FontStyle::Regular);
  const int sw = hint_font_.word_width(kStay, stay_len, FontStyle::Regular);

  buf.draw_text_proportional(btn0_pos - bw / 2, text_y, kBack, back_len, hint_font_, false);
  buf.draw_text_proportional(btn1_pos - sw / 2, text_y, kStay, stay_len, hint_font_, false);

  buf.set_rotation_transform(saved_rotation);
}

bool ReaderScreen::render_current_page(DrawBuffer& buf) {
  if (!open_ok_)
    return false;
  render_page_(buf);
  return true;
}

bool ReaderScreen::next_page_and_render(DrawBuffer& buf) {
  if (!open_ok_)
    return false;
  if (!next_page_())
    return false;
  if (grayscale_active_) {
    buf.revert_grayscale();
    grayscale_active_ = false;
  }
  render_page_(buf);
  return true;
}

bool ReaderScreen::is_open_ok() const {
  return open_ok_;
}

void ReaderScreen::bench_render(DrawBuffer& buf, int iterations) {
#ifdef ESP_PLATFORM
  if (!open_ok_) {
    ESP_LOGW("bench", "bench_render: no book open");
    return;
  }
  // Navigate to the start of the book.
  load_chapter_(0);
  page_pos_ = PagePosition{0, 0};

  const int n = (iterations > 200) ? 200 : (iterations < 1 ? 1 : iterations);
  long total_ms = 0;
  long min_ms = INT32_MAX, max_ms = 0;
  int pages_done = 0;

  for (int i = 0; i < n; ++i) {
    int64_t t0 = esp_timer_get_time();
    render_page_(buf);
    long ms = (long)((esp_timer_get_time() - t0) / 1000);
    int word_count = 0;
    for (const auto& ci : page_.items)
      if (const PageTextItem* ti = std::get_if<PageTextItem>(&ci))
        word_count += (int)ti->line.words.size();
    ESP_LOGI("bench", "page[%d/%d]: %ldms words=%d", i + 1, n, ms, word_count);
    total_ms += ms;
    if (ms < min_ms)
      min_ms = ms;
    if (ms > max_ms)
      max_ms = ms;
    ++pages_done;
    if (page_.at_chapter_end && chapter_idx_ + 1 >= mrb_.chapter_count())
      break;
    next_page_();
  }

  long avg_ms = pages_done > 0 ? total_ms / pages_done : 0;
  ESP_LOGI("bench", "RENDER_BENCH:pages=%d,min=%ldms,max=%ldms,avg=%ldms,total=%ldms", pages_done, min_ms, max_ms,
           avg_ms, total_ms);
#endif
}

size_t ReaderScreen::current_chapter_index() const {
  return chapter_idx_;
}

void ReaderScreen::render_text_(DrawBuffer& buf, const BitmapFontSet& fset, GrayPlane plane, bool white,
                                int left_padding) {
  uint8_t* render = buf.render_buf();
  for (const auto& ci : page_.items) {
    const PageTextItem* item = std::get_if<PageTextItem>(&ci);
    if (!item)
      continue;
    int baseline_y = static_cast<int>(item->y_offset) + item->baseline;
    buf.draw_layout_line(render, left_padding, baseline_y, item->line, fset, plane, white);
  }
}

void ReaderScreen::apply_grayscale_(DrawBuffer& buf) {
  const BitmapFontSet* fset = ext_font_set_ ? ext_font_set_ : (font_set_.valid() ? &font_set_ : nullptr);
  if (!fset || !fset->has_grayscale())
    return;

  // LSB plane → BW RAM (no refresh)
  buf.fill(false);
  render_text_(buf, *fset, GrayPlane::LSB, true, reader_settings_.h_padding());
  buf.write_ram_bw();

  // MSB plane → RED RAM (no refresh)
  buf.fill(false);
  render_text_(buf, *fset, GrayPlane::MSB, true, reader_settings_.h_padding());
  buf.write_ram_red();

  // Trigger grayscale refresh with custom LUT
  buf.grayscale_refresh();
}

bool ReaderScreen::next_page_() {
  if (page_.at_chapter_end) {
    if (chapter_idx_ + 1 < mrb_.chapter_count()) {
      load_chapter_(chapter_idx_ + 1);
      page_pos_ = PagePosition{0, 0};
      return true;
    }
    return false;
  }
  PagePosition next = page_.end;
  // If the page ended mid-image (offset > 0 into an image paragraph or
  // mid-promoted-inline-image), snap back to the start of that paragraph
  // so the next page shows the full image.
  // We only do this if the image wasn't the first thing on the current page,
  // to avoid infinite loops on images taller than the screen.
  if (next.offset > 0 && chapter_src_ && next.paragraph < chapter_src_->paragraph_count()) {
    if (page_pos_.paragraph != next.paragraph) {
      if (chapter_src_->paragraph(next.paragraph).type == ParagraphType::Image ||
          layout_engine_.is_mid_promoted_image(next))
        next.offset = 0;
    }
  }
  page_pos_ = next;
  return true;
}

bool ReaderScreen::prev_page_() {
  if (page_pos_ == PagePosition{0, 0}) {
    if (chapter_idx_ > 0) {
      load_chapter_(chapter_idx_ - 1);
      // Jump to the last page of the previous chapter using backward layout.
      const uint16_t end_para = static_cast<uint16_t>(chapter_src_->paragraph_count());
      layout_engine_.set_position(PagePosition{end_para, 0});
      auto pc = layout_engine_.layout_backward();
      page_pos_ = pc.start;
      return true;
    }
    return false;
  }

  // If the current page starts mid-image, snap end to the bottom of that image
  // so layout_backward produces the page ending at the image bottom — which
  // naturally includes the full image (rows 0..end) plus whatever text fits above.
  PagePosition end = layout_engine_.snap_to_image_end(page_pos_);
  layout_engine_.set_position(end);
  auto pc = layout_engine_.layout_backward();
  page_pos_ = pc.start;
  return true;
}

// ---------------------------------------------------------------------------
// Bookmark persistence
// ---------------------------------------------------------------------------

void ReaderScreen::save_position_() {
  if (pos_path_.empty())
    return;
  const std::string& path = pos_path_;
  FILE* f = std::fopen(path.c_str(), "w");
  if (!f)
    return;
  std::fprintf(f, "%u %u %u %u\n", static_cast<unsigned>(chapter_idx_), static_cast<unsigned>(page_pos_.paragraph),
               static_cast<unsigned>(page_pos_.offset), static_cast<unsigned>(page_pos_.text_offset));
  std::fclose(f);
}

void ReaderScreen::load_position_() {
  if (pos_path_.empty())
    return;

  // Try the current (hash-based) key first.
  FILE* f = std::fopen(pos_path_.c_str(), "r");
  bool migrating = false;

  // If not found, try the legacy slug key so existing .pos files still load.
  if (!f) {
    std::string legacy_key = make_book_key_legacy(mrb_.metadata(), path_.c_str());
    std::string legacy_path = std::string(data_dir_) + "/data/" + legacy_key + ".pos";
    if (legacy_path != pos_path_) {
      f = std::fopen(legacy_path.c_str(), "r");
      if (f) {
        migrating = true;
        MR_LOGI("reader", "Migrating legacy pos file: '%s' -> '%s'", legacy_path.c_str(), pos_path_.c_str());
#ifdef ESP_PLATFORM
        unlink(legacy_path.c_str());
#else
        std::remove(legacy_path.c_str());
#endif
      }
    }
  }

  if (!f)
    return;
  unsigned ch = 0, para = 0, line = 0, to = 0;
  int scanned = std::fscanf(f, "%u %u %u %u", &ch, &para, &line, &to);
  std::fclose(f);
  if (scanned >= 3) {
    saved_chapter_idx_ = ch;
    saved_page_pos_ = PagePosition{static_cast<uint16_t>(para), static_cast<uint16_t>(line), static_cast<uint32_t>(to)};
    MR_LOGI("reader", "Loaded pos ch=%u para=%u line=%u to=%u (scanned=%d)", ch, para, line, to, scanned);
    // Write to the new path only when migrating from the legacy key.
    if (migrating) {
      FILE* fw = std::fopen(pos_path_.c_str(), "w");
      if (fw) {
        std::fprintf(fw, "%u %u %u %u\n", ch, para, line, to);
        std::fclose(fw);
      }
    }
  }
}

}  // namespace microreader
