#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../content/mrb/MrbReader.h"
#include "ListMenuScreen.h"

namespace microreader {

// ---------------------------------------------------------------------------
// ReaderSettings — user-adjustable reader preferences
// Stored in ReaderScreen; mutated inline by ReaderOptionsScreen.
// ---------------------------------------------------------------------------
enum class ProgressStyle : uint8_t {
  None = 0,
  Percentage = 1,
  Bar = 2,
};

enum class ProgressScope : uint8_t {
  Book = 0,
  Chapter = 1,
};

enum class AlignOverride : uint8_t {
  Book = 0,
  Justify,
  Left,
  Center,
  Right,
};

enum class SpacingOverride : uint8_t {
  Book = 0,
  Spacing_0_8x,
  Spacing_0_9x,
  Spacing_1_0x,
  Spacing_1_1x,
  Spacing_1_2x,
};

struct ReaderSettings {
  AlignOverride align_override = AlignOverride::Book;
  SpacingOverride spacing_override = SpacingOverride::Spacing_1_0x;
  uint8_t padding_h_idx = 1;                           // horizontal padding preset index (left & right)
  uint8_t padding_v_idx = 1;                           // vertical top padding preset index
  uint8_t font_size_idx = 1;                           // base font size preset index (1 = Normal/24px)
  ProgressStyle progress_style = ProgressStyle::Bar;   // reading progress indicator style
  ProgressScope progress_scope = ProgressScope::Book;  // progress scope: whole book or current chapter
  bool override_publisher_fonts = false;               // ignore publisher's font sizes
  bool chapter_bar = false;                            // chapter progress bar along the very bottom edge

  static constexpr uint16_t kHPaddingPresets[] = {4, 12, 24, 40};
  static constexpr uint16_t kVPaddingPresets[] = {0, 4, 12, 20};

  static constexpr uint16_t kSpacingPercents[] = {0, 80, 90, 100, 110, 120};  // Index matches SpacingOverride

  static constexpr const char* kHPaddingNames[] = {"Narrow", "Normal", "Wide", "Wider"};
  static constexpr const char* kVPaddingNames[] = {"Tight", "Normal", "Loose", "Looser"};
  static constexpr const char* kAlignNames[] = {"Book", "Justify", "Left", "Center", "Right"};
  static constexpr const char* kSpacingNames[] = {"Book", "0.8x", "0.9x", "1.0x", "1.1x", "1.2x"};
  static constexpr const char* kFontSizeNames[] = {"20", "24", "26", "28", "30", "32", "34", "36"};

  static constexpr uint8_t kNumPresets = 4;
  static constexpr uint8_t kNumAlignPresets = 5;
  static constexpr uint8_t kNumSpacingPresets = 6;
  static constexpr uint8_t kNumFontSizePresets = 8;

  uint16_t h_padding() const {
    return kHPaddingPresets[padding_h_idx];
  }
  uint16_t v_padding() const {
    return kVPaddingPresets[padding_v_idx];
  }
  uint16_t line_height_multiplier_percent() const {
    return kSpacingPercents[static_cast<uint8_t>(spacing_override)];
  }
  // Chapter bar geometry: an outlined bar kChapterBarH px tall at the bottom
  // edge, with kChapterBarGap px between it and anything above.
  static constexpr int kChapterBarH = 7;
  static constexpr int kChapterBarGap = 4;
  // Extra bottom padding the chapter bar needs (0 when it's off).
  uint16_t chapter_bar_reserve() const {
    return chapter_bar ? static_cast<uint16_t>(kChapterBarH + kChapterBarGap) : 0;
  }

  // Bottom padding reserved for the progress indicator.
  uint16_t progress_bottom() const {
    switch (progress_style) {
      case ProgressStyle::Percentage:
        return 18;
      case ProgressStyle::Bar:
        return 8;
      default:
        return 6;
    }
  }
};

// A hyperlink found on the current reader page.
// Used to pass link info from ReaderScreen → ReaderOptionsScreen → LinksScreen.
struct PageLink {
  std::string label;  // full anchor text of the link (all words concatenated)
  std::string href;   // "path|fragment" (see EpubParser.cpp)
};

// In-reader options menu — shown when the user presses Button1 while reading.
// Populated by ReaderScreen before being pushed so it reflects the current
// reading context (TOC availability, page links, etc.).
//
// Currently supports:
//   - Justify on/off, H-Margin, V-Margin, Line spacing (inline cycling)
//   - "Chapters" (TOC navigation) → replaces this screen with ChapterSelectScreen
//
// Usage:
//   app_->reader_options()->set_settings(&reader_settings_);
//   app_->reader_options()->populate(mrb_.toc(), chapter_idx, page_pos_.paragraph);
//   app_->push_screen(ScreenId::ReaderOptions);
class ReaderOptionsScreen final : public ListMenuScreen {
 public:
  ReaderOptionsScreen() = default;

  const char* name() const override {
    return "Options";
  }

  // Set the settings object to read/write (must outlive this screen).
  void set_settings(ReaderSettings* s) {
    settings_ = s;
  }

  // Populate before pushing. Pass toc (may be empty — "Chapters" hidden when empty).
  void populate(const TableOfContents& toc, uint16_t current_chapter, uint16_t current_para,
                const std::string& fallback_title, int book_progress_pct, int chapter_progress_pct);

  // Set links found on the current reader page (called after populate).
  // spine_files maps chapter index → base filename for href resolution.
  // mrb is used to resolve fragment anchors.
  void set_page_links(const std::vector<PageLink>& links, const std::vector<std::string>& spine_files,
                      const MrbReader& mrb);

  // Identify the open book so "Finished" can be toggled (call after populate).
  void set_book_identity(std::string path, std::string title, std::string author) {
    book_path_ = std::move(path);
    book_meta_title_ = std::move(title);
    book_author_ = std::move(author);
  }

  void stop() override {
    toc_ = nullptr;
  }

  void pause() override {}  // keep toc_ alive so resume() → start() can rebuild the list

  void update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override {
    buf_ = &buf;
    ListMenuScreen::update(buttons, buf, runtime);
  }

  void start(DrawBuffer& buf, IRuntime& runtime) override;

 protected:
  void on_start() override;
  void on_select(int index) override;

 private:
  ReaderSettings* settings_ = nullptr;
  DrawBuffer* buf_ = nullptr;

  // Item indices (-1 = not shown).
  int idx_justify_ = -1;
  int idx_padding_h_ = -1;
  int idx_padding_v_ = -1;
  int idx_line_spacing_ = -1;
  int idx_font_size_ = -1;
  int idx_progress_ = -1;
  int idx_progress_scope_ = -1;
  int idx_pub_fonts_ = -1;
  int idx_chapters_ = -1;
  int idx_rotate_display_ = -1;
  int idx_links_ = -1;
  int idx_finished_ = -1;
  int idx_chapter_bar_ = -1;

  std::string book_path_;
  std::string book_meta_title_;
  std::string book_author_;

  // Page links set by ReaderScreen before pushing this screen.
  std::vector<PageLink> page_links_;

  // Re-populate item labels after an inline setting change, restoring selection.
  void refresh_items_(int restore_selection);

  // Saved before rebuilding the list so on_start() can restore the correct item.
  int prev_selected_ = 0;
  int prev_idx_links_ = -1;

  std::string book_title_;
  std::string book_title1_buf_;
  std::string book_title2_buf_;
  std::string chapter_title_;

  int book_progress_pct_ = 0;
  int chapter_progress_pct_ = 0;

  const TableOfContents* toc_ = nullptr;
};

}  // namespace microreader
