#pragma once

#include <functional>
#include <memory>

#include "../Input.h"
#include "../content/BitmapFont.h"
#include "../content/Book.h"
#include "../content/TextLayout.h"
#include "../content/mrb/MrbConverter.h"
#include "../content/mrb/MrbReader.h"
#include "../display/DrawBuffer.h"
#include "IScreen.h"
#include "ReaderOptionsScreen.h"

namespace microreader {

// Simple EPUB page viewer.
// Renders text using the 8Ã—8 bitmap font scaled 2Ã— (16Ã—16 glyphs).
// Button2 = next page, Button3 = prev page, Button0 = back to menu.
// Button1 = open chapter list (if TOC available).
class ReaderScreen final : public IScreen {
 public:
  // Get the current book path
  std::string get_path() {
    return path_;
  }
  ReaderScreen() = default;
  explicit ReaderScreen(std::string epub_path) : path_(std::move(epub_path)) {}

  void set_path(std::string epub_path) {
    path_ = std::move(epub_path);
  }
  bool has_path() const {
    return !path_.empty();
  }
  void set_data_dir(std::string dir) {
    data_dir_ = std::move(dir);
  }

  // Set the proportional bitmap font for rendering. If null, falls back to
  // the builtin 8Ã—8 bitmap font at 2Ã— scale. The font data must outlive
  // this screen.
  void set_font(const BitmapFont* font) {}

  // Set the full font set (Small/Normal/Large). Font data must outlive this screen.
  void set_fonts(const BitmapFontSet* fonts) {
    ext_font_set_ = fonts;
  }

  // Export helpers.
  bool render_current_page(DrawBuffer& buf);
  bool next_page_and_render(DrawBuffer& buf);
  bool is_open_ok() const;
  // When set, start() will pop back to the book list instead of converting if
  // the MRB cache is missing. The flag is consumed (reset to false) in start().
  void set_cache_only(bool v) { cache_only_ = v; }

  // Render benchmark: calls render_page_ `iterations` times on the current page
  // and logs timing stats (per-iteration + summary). ESP32-only; no-op on desktop.
  void bench_render(DrawBuffer& buf, int iterations = 100);
  size_t current_chapter_index() const;

  // Test accessors — expose internal state so tests can drive the real screen
  // without duplicating its logic.
  const std::vector<PageLink>& test_page_links() const {
    return page_links_;
  }
  const MrbReader& test_mrb() const {
    return mrb_;
  }

  const char* name() const override {
    return "Reader";
  }

  std::string book_cache_dir() {
    return book_cache_dir_;
  }

  void start(DrawBuffer& buf, IRuntime& runtime) override;
  void stop() override;
  // pause(): keep mrb_ open while a child screen (options/chapter) is active.
  void pause() override {}
  // resume(): return from a child screen — handle any pending navigation, then re-render.
  void resume(DrawBuffer& buf, IRuntime& runtime) override;
  void update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;

  // Layout constants â€” exposed so tests and tools can build matching PageOptions.
  static constexpr int kScale = 2;
  static constexpr int kGlyphW = 8;
  static constexpr int kGlyphH = 8;
  static constexpr int kPaddingTop = 0;
  static constexpr int kPaddingRight = 12;
  static constexpr int kPaddingBottom = 14;
  static constexpr int kPaddingLeft = 12;
  static constexpr int kParaSpacing = 8;

  // Build the fixed fallback font used when no proportional font is loaded.
  static FixedFont make_fixed_font() {
    return FixedFont(kGlyphW * kScale, kGlyphH * kScale + 4);
  }

  // Build PageOptions matching the reader's layout configuration.
  // Pass settings to get the correct bottom padding for the active progress style.
  static PageOptions make_page_opts(const ReaderSettings* settings = nullptr, int width = DrawBuffer::kWidth,
                                    int height = DrawBuffer::kHeight) {
    PageOptions opts(static_cast<uint16_t>(width), static_cast<uint16_t>(height), kPaddingTop, kParaSpacing,
                     Alignment::Start);
    opts.padding_right = kPaddingRight;
    opts.padding_bottom = settings ? settings->progress_bottom() : kPaddingBottom;
    opts.padding_left = kPaddingLeft;
    opts.center_text = true;
    return opts;
  }

 private:
  BitmapFontSet font_set_;                       // owned set (for single-font set_font() path)
  const BitmapFontSet* ext_font_set_ = nullptr;  // external set (from set_fonts())
  BitmapFont hint_font_;                         // UI font for nav-history button hints
  std::string path_;
  std::string data_dir_;
  std::string book_cache_dir_;
  std::string mrb_path_;
  std::string pos_path_;       // path to .pos bookmark: <data_dir>/data/<book_key>.pos
  std::string book_key_;       // sanitized title (content-derived), drives .pos filename
  DrawBuffer* buf_ = nullptr;  // set in start(), cleared in stop()
  Book book_;
  MrbReader mrb_;
  std::unique_ptr<MrbChapterSource> chapter_src_;
  size_t chapter_idx_ = 0;
  TextLayout layout_engine_;
  PagePosition page_pos_;
  PageContent page_;
  bool open_ok_ = false;
  bool buf_was_touched_ = false;
  bool cache_only_ = false;

  // Navigation history: stack of positions pushed before following a hyperlink.
  struct NavHistoryEntry {
    size_t chapter_idx;
    PagePosition page_pos;
  };
  static constexpr size_t kMaxNavHistory = 8;
  std::vector<NavHistoryEntry> nav_history_;

  // Reader options menu â€” pushed when user presses Button1.
  // Prep (set_settings + populate) happens before calling app_->push_screen(ReaderOptions).
  ReaderSettings reader_settings_;  // user-adjustable settings, mutated by reader_options_

  // Saved position (survives stop()) so we can restore after chapter select cancel.
  size_t saved_chapter_idx_ = 0;
  PagePosition saved_page_pos_;

  ImageSizeQuery image_size_fn_;
  bool grayscale_pending_ = false;
  bool grayscale_active_ = false;
  std::vector<PageLink> page_links_;

  // Returns the filename stem of path_ (no directory, no extension).
  std::string book_stem_() const;

  bool decode_image_to_buffer_(uint16_t img_key, uint32_t offset, DrawBuffer& buf, int dest_x, int dest_y,
                               uint16_t max_w, uint16_t max_h, uint16_t src_y = 0, uint16_t clip_h = 0);
  // Render page content (BW only). Sets grayscale_pending_ if font has grayscale.
  void render_page_(DrawBuffer& buf);
  // Returns the bottom padding required for the progress indicator and nav hints.
  uint16_t bottom_padding_(bool landscape) const;
  // Draws the progress indicator and nav hints into the bottom margin.
  void draw_bottom_(DrawBuffer& buf, bool landscape);
  // Build page_links_ from current page_ content. Called from render_page_().
  void collect_page_links_();
  // Deferred grayscale pass: writes LSB/MSB planes to BW/RED RAM and triggers
  // grayscale LUT refresh. Called from update() after BW refresh is committed.
  void apply_grayscale_(DrawBuffer& buf);
  void render_text_(DrawBuffer& buf, const BitmapFontSet& fset, GrayPlane plane, bool white, int left_padding);
  bool next_page_();
  bool prev_page_();
  void load_chapter_(size_t idx);
  void save_position_();
  void load_position_();

 public:
  // Access to user-adjustable display settings (read/write by Application for persistence).
  ReaderSettings& reader_settings() {
    return reader_settings_;
  }
  const ReaderSettings& reader_settings() const {
    return reader_settings_;
  }

  // Links found on the current page (populated after render_page_()).
  const std::vector<PageLink>& page_links() const {
    return page_links_;
  }

  // Returns progress percentage 0-100 based on read characters (whole book)
  int progress_pct() const {
    if (mrb_.paragraph_count() == 0)
      return 0;
    const bool is_last_chapter = chapter_idx_ + 1 >= mrb_.chapter_count();
    if (page_.at_chapter_end && is_last_chapter)
      return 100;
    const uint64_t total_chars = mrb_.total_char_count();
    uint64_t chars_before = 0;
    for (size_t i = 0; i < chapter_idx_; ++i)
      chars_before += mrb_.chapter_char_count(static_cast<uint16_t>(i));
    const uint64_t cur =
        chars_before + (chapter_src_ ? chapter_src_->char_before_para(page_pos_.paragraph) : 0) + page_pos_.text_offset;
    return total_chars > 0 ? static_cast<int>(cur * 100u / total_chars) : 0;
  }

  // Returns progress percentage 0-100 within the current chapter
  int chapter_progress_pct() const {
    if (mrb_.paragraph_count() == 0)
      return 0;
    if (page_.at_chapter_end)
      return 100;
    const uint64_t chapter_chars = mrb_.chapter_char_count(static_cast<uint16_t>(chapter_idx_));
    const uint64_t cur =
        (chapter_src_ ? chapter_src_->char_before_para(page_pos_.paragraph) : 0) + page_pos_.text_offset;
    return chapter_chars > 0 ? static_cast<int>(cur * 100u / chapter_chars) : 0;
  }
};

}  // namespace microreader
