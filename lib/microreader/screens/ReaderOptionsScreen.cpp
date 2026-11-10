#include "ReaderOptionsScreen.h"

#include <climits>
#include <cstring>

#include "../Application.h"

namespace microreader {
constexpr uint16_t ReaderSettings::kHPaddingPresets[];
constexpr uint16_t ReaderSettings::kVPaddingPresets[];
constexpr uint16_t ReaderSettings::kSpacingPercents[];
constexpr uint8_t ReaderSettings::kNumAlignPresets;
constexpr uint8_t ReaderSettings::kNumSpacingPresets;
constexpr uint8_t ReaderSettings::kNumFontSizePresets;
constexpr const char* ReaderSettings::kHPaddingNames[];
constexpr const char* ReaderSettings::kVPaddingNames[];
constexpr const char* ReaderSettings::kAlignNames[];
constexpr const char* ReaderSettings::kSpacingNames[];
constexpr const char* ReaderSettings::kFontSizeNames[];

// ---------------------------------------------------------------------------

void ReaderOptionsScreen::populate(const TableOfContents& toc, uint16_t current_chapter, uint16_t current_para,
                                   const std::string& fallback_title, int book_progress_pct, int chapter_progress_pct) {
  toc_ = &toc;
  book_title_ = fallback_title;
  chapter_title_ = fallback_title;
  int best_match = -1;
  for (size_t i = 0; i < toc_->entries.size(); ++i) {
    if (toc_->entries[i].file_idx < current_chapter ||
        (toc_->entries[i].file_idx == current_chapter && toc_->entries[i].para_index <= current_para)) {
      best_match = static_cast<int>(i);
    }
  }
  if (best_match >= 0) {
    chapter_title_ = toc_->entries[best_match].label.to_string(toc_->pool);
  }

  book_progress_pct_ = book_progress_pct;
  chapter_progress_pct_ = chapter_progress_pct;
  if (toc_ && !toc_->entries.empty() && app_) {
    app_->chapter_select()->populate(*toc_, current_chapter, current_para);
    app_->chapter_select()->clear_pending();
  }
}

// Build a "Label: Value" string into a fixed buffer.
static const char* fmt_setting(char* buf, size_t bufsz, const char* label, const char* value) {
  snprintf(buf, bufsz, "%s: %s", label, value);
  return buf;
}

void ReaderOptionsScreen::start(DrawBuffer& buf, IRuntime& runtime) {
  buf_ = &buf;
  // Capture current selection and Links position before the base class
  // calls on_start(), which rebuilds the list.
  prev_selected_ = selected_index();
  prev_idx_links_ = idx_links_;
  ListMenuScreen::start(buf, runtime);
}

void ReaderOptionsScreen::set_page_links(const std::vector<PageLink>& links,
                                         const std::vector<std::string>& spine_files, const MrbReader& mrb) {
  page_links_ = links;
  // Populate the links screen now while the MrbReader is still open.
  // (ReaderScreen::stop() will close mrb before we get to on_select.)
  if (app_)
    app_->links_screen()->populate(links, spine_files, mrb);
}

void ReaderOptionsScreen::on_start() {
  title2_ = nullptr;
  book_title1_buf_.clear();

  if (header_font_.valid()) {
    const int max_line_w = buf_->width() - 24;
    const int total_w = header_font_.word_width(book_title_.c_str(), book_title_.length(), FontStyle::Regular);
    if (total_w <= max_line_w) {
      title_ = book_title_.c_str();
    } else {
      std::vector<std::string> words;
      std::string w;
      for (size_t i = 0; i <= book_title_.length(); ++i) {
        if (i == book_title_.length() || book_title_[i] == ' ') {
          if (!w.empty())
            words.push_back(w);
          w.clear();
        } else {
          w += book_title_[i];
        }
      }

      std::vector<int> word_w;
      word_w.reserve(words.size());
      int all_w = 0;
      for (const auto& word : words) {
        int ww = static_cast<int>(header_font_.word_width(word.c_str(), word.length(), FontStyle::Regular));
        word_w.push_back(ww);
        all_w += ww;
      }
      int spaces = static_cast<int>(words.size()) - 1;
      if (spaces > 0)
        all_w += spaces * static_cast<int>(header_font_.word_width(" ", 1, FontStyle::Regular));

      if (all_w <= max_line_w) {
        title_ = book_title_.c_str();
      } else {
        int space_w = static_cast<int>(header_font_.word_width(" ", 1, FontStyle::Regular));
        int ellipsis_w = static_cast<int>(header_font_.word_width("...", 3, FontStyle::Regular));

        int best_split = 1;
        int best_diff = INT_MAX;
        int l1_w = 0;
        for (size_t i = 0; i < words.size(); ++i) {
          if (i > 0)
            l1_w += space_w;
          l1_w += word_w[i];
          if (l1_w > max_line_w)
            break;
          if (i + 1 < words.size()) {
            int l2_w = 0;
            for (size_t j = i + 1; j < words.size(); ++j) {
              if (j > i + 1)
                l2_w += space_w;
              l2_w += word_w[j];
            }
            int diff = std::abs(l1_w - l2_w);
            if (diff < best_diff) {
              best_diff = diff;
              best_split = static_cast<int>(i) + 1;
            }
          }
        }

        std::string line1, line2;
        for (int i = 0; i < best_split; ++i) {
          if (i > 0)
            line1 += ' ';
          line1 += words[i];
        }
        for (int i = best_split; i < static_cast<int>(words.size()); ++i) {
          if (i > best_split)
            line2 += ' ';
          line2 += words[i];
        }

        if (header_font_.word_width(line2.c_str(), line2.length(), FontStyle::Regular) <= max_line_w) {
          book_title2_buf_ = line2;
          title2_ = book_title2_buf_.c_str();
        } else {
          int l2_w = 0;
          int trunc_at = 0;
          for (int i = best_split; i < static_cast<int>(words.size()); ++i) {
            int word_with_space = word_w[i] + ((i > best_split) ? space_w : 0);
            if (l2_w + word_with_space + ellipsis_w > max_line_w)
              break;
            l2_w += word_with_space;
            trunc_at = i + 1;
          }
          line2.clear();
          for (int i = best_split; i < trunc_at; ++i) {
            if (i > best_split)
              line2 += ' ';
            line2 += words[i];
          }
          line2 += "...";
          book_title2_buf_ = line2;
          title2_ = book_title2_buf_.c_str();
        }

        book_title1_buf_ = line1;
        title_ = book_title1_buf_.c_str();
      }
    }
  } else {
    if (book_title_.length() > 30) {
      book_title1_buf_ = book_title_.substr(0, 27) + "...";
      title_ = book_title1_buf_.c_str();
    } else {
      title_ = book_title_.c_str();
    }
  }

  subtitle_ = chapter_title_;
  if (subtitle_.length() > 42) {
    subtitle_ = subtitle_.substr(0, 39) + "...";
  }

  char pct_str[48];
  snprintf(pct_str, sizeof(pct_str), "Chapter: %d%%  |  Book: %d%%", chapter_progress_pct_, book_progress_pct_);
  subtitle2_ = pct_str;
  subtitle3_ = "";

  clear_items();
  idx_justify_ = idx_padding_h_ = idx_padding_v_ = idx_line_spacing_ = idx_progress_ = idx_progress_scope_ =
      idx_chapters_ = idx_pub_fonts_ = idx_rotate_display_ = idx_links_ = idx_finished_ = idx_chapter_bar_ = -1;

  char tmp[40];

  bool has_toc = toc_ && !toc_->entries.empty();
  // "Chapters" goes at the top so it's easy to reach.
  if (app_ && has_toc) {
    idx_chapters_ = count();
    add_item("Chapters");
  }

  // "Links" on this page — shown only when there are hyperlinks.
  if (!page_links_.empty()) {
    idx_links_ = count();
    char link_label[40];
    snprintf(link_label, sizeof(link_label), "Links (%d)", static_cast<int>(page_links_.size()));
    add_item(link_label);
  }

  // Move the book to / from the Finished shelf.
  if (app_ && !book_path_.empty()) {
    idx_finished_ = count();
    const bool done = app_->finished_books().contains(book_path_, book_meta_title_, book_author_);
    add_item(fmt_setting(tmp, sizeof(tmp), "Finished", done ? "Yes" : "No"));
  }

  // Separator between chapter navigation and text layout settings.
  if (settings_ && app_ && (has_toc || idx_finished_ >= 0))
    add_separator();

  if (settings_) {
    idx_font_size_ = count();
    if (app_ && app_->font_manager() && app_->font_manager()->valid()) {
      auto* fonts = app_->font_manager()->font_set();
      int sz = 0;
      if (fonts && settings_->font_size_idx < fonts->num_fonts()) {
        auto* f = fonts->get_font(settings_->font_size_idx);
        if (f) {
          sz = f->nominal_size();
          if (sz == 0)
            sz = f->y_advance();
        }
      }
      char val[16];
      if (sz > 0)
        snprintf(val, sizeof(val), "%d", sz);
      else
        snprintf(val, sizeof(val), "Unknown");
      add_item(fmt_setting(tmp, sizeof(tmp), "Font Size", val));
    } else {
      add_item(fmt_setting(tmp, sizeof(tmp), "Font Size", ReaderSettings::kFontSizeNames[settings_->font_size_idx]));
    }

    idx_pub_fonts_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "Publisher Sizes", settings_->override_publisher_fonts ? "Off" : "On"));

    idx_line_spacing_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "Line spacing",
                         ReaderSettings::kSpacingNames[static_cast<uint8_t>(settings_->spacing_override)]));

    idx_justify_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "Alignment",
                         ReaderSettings::kAlignNames[static_cast<uint8_t>(settings_->align_override)]));

    add_separator();

    idx_padding_h_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "H-Margin", ReaderSettings::kHPaddingNames[settings_->padding_h_idx]));

    idx_padding_v_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "V-Margin", ReaderSettings::kVPaddingNames[settings_->padding_v_idx]));

    add_separator();

    idx_progress_ = count();
    const char* prog_name = settings_->progress_style == ProgressStyle::None         ? "None"
                            : settings_->progress_style == ProgressStyle::Percentage ? "Percent"
                                                                                     : "Bar";
    add_item(fmt_setting(tmp, sizeof(tmp), "Progress", prog_name));

    if (settings_->progress_style != ProgressStyle::None) {
      idx_progress_scope_ = count();
      add_item(fmt_setting(tmp, sizeof(tmp), "Progress Scope",
                           settings_->progress_scope == ProgressScope::Chapter ? "Chapter" : "Book"));
    }

    idx_chapter_bar_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "Chapter Bar", settings_->chapter_bar ? "On" : "Off"));

    idx_rotate_display_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "Display", app_ && app_->rotate_display() ? "Landscape" : "Portrait"));
  }

  // Restore selection, adjusting for Links appearing or disappearing.
  int sel = prev_selected_;
  if (prev_idx_links_ == -1 && idx_links_ != -1) {
    // Links appeared: everything at idx_links_ and below shifted down by 1.
    if (sel >= idx_links_)
      sel++;
  } else if (prev_idx_links_ != -1 && idx_links_ == -1) {
    // Links disappeared: cursor was on Links or below it — move up by 1.
    if (sel >= prev_idx_links_)
      sel--;
  }
  int max_sel = count() - 1;
  if (max_sel >= 0) {
    if (sel < 0)
      sel = 0;
    if (sel > max_sel)
      sel = max_sel;
  }
  set_selected(sel);
}

void ReaderOptionsScreen::refresh_items_(int restore_selection) {
  prev_selected_ = restore_selection;
  prev_idx_links_ = idx_links_;
  on_start();  // on_start() applies shift correction and calls set_selected().
}

void ReaderOptionsScreen::on_select(int index) {
  if (!settings_) {
    if (index == idx_chapters_) {
      app_->push_screen(ScreenId::ChapterSelect);
      return;
    }
    return;
  }

  if (index == idx_finished_) {
    if (app_) {
      const bool done = app_->finished_books().contains(book_path_, book_meta_title_, book_author_);
      app_->finished_books().set(book_path_, book_meta_title_, book_author_, !done, app_->local_time_now());
    }
    refresh_items_(index);
    return;
  }
  if (index == idx_chapter_bar_) {
    settings_->chapter_bar = !settings_->chapter_bar;
    refresh_items_(index);
    return;
  }
  if (index == idx_justify_) {
    settings_->align_override = static_cast<AlignOverride>((static_cast<uint8_t>(settings_->align_override) + 1) %
                                                           ReaderSettings::kNumAlignPresets);
    refresh_items_(index);
    return;
  }
  if (index == idx_pub_fonts_) {
    settings_->override_publisher_fonts = !settings_->override_publisher_fonts;
    refresh_items_(index);
    return;
  }
  if (index == idx_font_size_) {
    uint8_t max_idx = ReaderSettings::kNumFontSizePresets;
    if (app_ && app_->font_manager() && app_->font_manager()->valid()) {
      auto* fonts = app_->font_manager()->font_set();
      if (fonts && fonts->num_fonts() > 0)
        max_idx = static_cast<uint8_t>(fonts->num_fonts());
    }
    settings_->font_size_idx = static_cast<uint8_t>((settings_->font_size_idx + 1) % max_idx);
    refresh_items_(index);
    return;
  }
  if (index == idx_padding_h_) {
    settings_->padding_h_idx = static_cast<uint8_t>((settings_->padding_h_idx + 1) % ReaderSettings::kNumPresets);
    refresh_items_(index);
    return;
  }
  if (index == idx_padding_v_) {
    settings_->padding_v_idx = static_cast<uint8_t>((settings_->padding_v_idx + 1) % ReaderSettings::kNumPresets);
    refresh_items_(index);
    return;
  }
  if (index == idx_line_spacing_) {
    settings_->spacing_override = static_cast<SpacingOverride>((static_cast<uint8_t>(settings_->spacing_override) + 1) %
                                                               ReaderSettings::kNumSpacingPresets);
    refresh_items_(index);
    return;
  }
  if (index == idx_progress_) {
    settings_->progress_style = static_cast<ProgressStyle>((static_cast<uint8_t>(settings_->progress_style) + 1) % 3);
    refresh_items_(index);
    return;
  }
  if (index == idx_progress_scope_) {
    settings_->progress_scope =
        settings_->progress_scope == ProgressScope::Book ? ProgressScope::Chapter : ProgressScope::Book;
    refresh_items_(index);
    return;
  }
  if (index == idx_rotate_display_) {
    if (app_ && buf_) {
      bool v = !app_->rotate_display();
      app_->set_rotate_display(v);
      buf_->set_rotation(v ? Rotation::Deg0 : Rotation::Deg90);
      refresh_items_(index);
    }
    return;
  }
  if (index == idx_chapters_) {
    app_->push_screen(ScreenId::ChapterSelect);
    return;
  }
  if (index == idx_links_) {
    app_->push_screen(ScreenId::Links);
    return;
  }
  return;
}

}  // namespace microreader
