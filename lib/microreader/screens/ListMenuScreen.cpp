#include "ListMenuScreen.h"

#include <algorithm>
#include <cstring>

#include "../Application.h"
#include "../display/ui_font_header.h"
#include "../display/ui_font_large.h"
#include "../display/ui_font_medium.h"
#include "../display/ui_font_small.h"

namespace microreader {

int ListMenuScreen::font_size_idx_ = 0;

static constexpr int kHeaderY = 15;         // top padding before the title text
static constexpr int kHeaderBottomGap = 4;  // gap between last header line and first list item
// The hint row (nav glyphs + battery bar) is centred on a line kHintCenterY pixels above the
// screen bottom.  kBottomEdgePad is the gap from that centre line to the absolute screen edge.
// kBottomAreaH (= centre + pad) is the total reserved strip; the list stays above it.
static constexpr int kHintCenterY = 8;     // px from screen bottom to centre of the hint row
static constexpr int kBottomEdgePad = 11;  // px from hint centre to absolute screen edge
static constexpr int kBottomAreaH = kHintCenterY + kBottomEdgePad;
static constexpr int kSubtitleGap = 3;      // gap between bottom of title block and first subtitle
static constexpr int kSubtitleSpacing = 6;  // extra pixels between consecutive subtitles (added to y_advance)
static constexpr int kItemSpacing = 6;      // vertical gap between list item rows

void ListMenuScreen::start(DrawBuffer& buf, IRuntime& runtime) {
  buf_ = &buf;
  runtime_ = &runtime;
  // Re-init ui_font_ whenever the font_size_idx_ setting may have changed.
  ui_font_ = BitmapFont{};
  if (font_size_idx_ == 1)
    ui_font_.init(kFontData_ui_medium_mbf, kFontData_ui_medium_mbf_size);
  else if (font_size_idx_ == 2)
    ui_font_.init(kFontData_ui_large_mbf, kFontData_ui_large_mbf_size);
  else
    ui_font_.init(kFontData_ui_small_mbf, kFontData_ui_small_mbf_size);
  if (!header_font_.valid())
    header_font_.init(kFontData_ui_header_mbf, kFontData_ui_header_mbf_size);
  back_held_ = false;
  back_hold_frames_ = 0;
  const int prev_selected = selected_;
  clear_items();
  on_start_set_selection_ = false;
  on_start();
  // Restore selection if on_start() didn't explicitly call set_selected()
  // (e.g. returning from a sub-screen after navigating away).
  if (!on_start_set_selection_ && initial_selection_ >= 0 && initial_selection_ < count()) {
    selected_ = initial_selection_;
    initial_selection_ = -1;  // Only use once
  } else if (!on_start_set_selection_ && prev_selected > 0 && prev_selected < count()) {
    selected_ = prev_selected;
  }
  if (on_start_set_selection_ || selected_ > 0)
    center_on_selected_();
  else
    ensure_visible_();
  while (selected_ < count() && selected_ < (int)separators_.size() && separators_[selected_])
    ++selected_;
  draw_all_(buf, runtime.battery_percentage());
}

void ListMenuScreen::ensure_visible_() {
  if (!ui_font_.valid() || count() == 0 || !buf_)
    return;
  const int line_h = ui_font_.y_advance() + kItemSpacing;
  const int header_h = compute_header_h_();
  const int available_h = buf_->height() - header_h - kBottomAreaH;
  // N items occupy N*y_advance + (N-1)*kItemSpacing = N*line_h - kItemSpacing pixels.
  // So max N where that fits: N <= (available_h + kItemSpacing) / line_h.
  const int visible = available_h > 0 ? (available_h + kItemSpacing) / line_h : 0;
  if (visible <= 0)
    return;
  if (selected_ < scroll_offset_)
    scroll_offset_ = selected_;
  else if (selected_ >= scroll_offset_ + visible)
    scroll_offset_ = selected_ - visible + 1;

  // Scroll padding: keep at least 2 items visible above/below the selection.
  static constexpr int kPad = 2;
  if (selected_ - scroll_offset_ < kPad && scroll_offset_ > 0)
    scroll_offset_ = std::max(0, selected_ - kPad);
  else if (selected_ - scroll_offset_ > visible - 1 - kPad) {
    const int max_scroll = count() > visible ? count() - visible : 0;
    scroll_offset_ = std::min(max_scroll, selected_ - (visible - 1 - kPad));
  }
}

void ListMenuScreen::center_on_selected_() {
  if (!ui_font_.valid() || count() == 0 || !buf_)
    return;
  const int line_h = ui_font_.y_advance() + kItemSpacing;
  const int header_h = compute_header_h_();
  const int available_h = buf_->height() - header_h - kBottomAreaH;
  const int visible = available_h > 0 ? (available_h + kItemSpacing) / line_h : 0;
  if (visible <= 0)
    return;
  // Center the selection: put it in the middle of the visible window.
  int offset = selected_ - visible / 2;
  const int max_scroll = count() > visible ? count() - visible : 0;
  if (offset < 0)
    offset = 0;
  if (offset > max_scroll)
    offset = max_scroll;
  scroll_offset_ = offset;
}

// ─────────────────────────────────────────────────────────────────────────────
// draw_all_: orchestrates the four drawing passes.
// ─────────────────────────────────────────────────────────────────────────────
void ListMenuScreen::draw_all_(DrawBuffer& buf, std::optional<uint8_t> battery_pct) const {
  const int W = buf.width();
  const int H = buf.height();
  buf.fill(true);
  const int header_h = draw_header_(buf, W, H);
  const int bottom_h = draw_bottom_(buf, W, H, battery_pct);  // == kBottomAreaH
  draw_list_(buf, W, H, header_h, bottom_h);
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. compute_header_h_: returns the header height without drawing anything.
//    Used by ensure_visible_() and center_on_selected_() before the draw pass.
// ─────────────────────────────────────────────────────────────────────────────
int ListMenuScreen::compute_header_h_() const {
  int subtitle_h = 0;
  if (ui_font_.valid()) {
    if (!subtitle_.empty())
      subtitle_h += ui_font_.y_advance() + kSubtitleSpacing;
    if (!subtitle2_.empty())
      subtitle_h += ui_font_.y_advance() + kSubtitleSpacing;
    if (!subtitle3_.empty())
      subtitle_h += ui_font_.y_advance() + kSubtitleSpacing;
  }
  const int hfh = header_font_.valid() ? header_font_.y_advance() : 0;
  const int t2h = (title2_ && header_font_.valid()) ? header_font_.y_advance() : 0;
  return kHeaderY + hfh + t2h + subtitle_h + kHeaderBottomGap;
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. draw_header_: draws title, title2, and subtitles.
//    Returns header_h = pixels from y=0 to where list items begin.
// ─────────────────────────────────────────────────────────────────────────────
int ListMenuScreen::draw_header_(DrawBuffer& buf, int W, int H) const {
  if (title_ && header_font_.valid()) {
    const size_t len = std::strlen(title_);
    const int tw = header_font_.word_width(title_, len, FontStyle::Regular);
    buf.draw_text_proportional((W - tw) / 2, kHeaderY + header_font_.baseline(), title_, header_font_, false);
  }
  if (title2_ && header_font_.valid()) {
    const size_t len = std::strlen(title2_);
    const int tw = header_font_.word_width(title2_, len, FontStyle::Regular);
    const int y = kHeaderY + header_font_.y_advance();
    buf.draw_text_proportional((W - tw) / 2, y + header_font_.baseline(), title2_, header_font_, false);
  }

  // Subtitles stack from the bottom edge of the title block.
  const int title_bottom = kHeaderY + (header_font_.valid() ? header_font_.y_advance() : 0) +
                           ((title2_ && header_font_.valid()) ? header_font_.y_advance() + 4 : 0);
  int sub_y = title_bottom + kSubtitleGap;
  int subtitle_h = 0;
  auto draw_subtitle = [&](const std::string& text) {
    if (text.empty() || !ui_font_.valid())
      return;
    const int sw = ui_font_.word_width(text.c_str(), text.size(), FontStyle::Regular);
    buf.draw_text_proportional((W - sw) / 2, sub_y + ui_font_.baseline(), text.c_str(), text.size(), ui_font_, false);
    subtitle_h += ui_font_.y_advance() + kSubtitleSpacing;
    sub_y += ui_font_.y_advance() + kSubtitleSpacing;
  };
  draw_subtitle(subtitle_);
  draw_subtitle(subtitle2_);
  draw_subtitle(subtitle3_);

  const int hfh = header_font_.valid() ? header_font_.y_advance() : 0;
  const int t2h = (title2_ && header_font_.valid()) ? header_font_.y_advance() : 0;
  return kHeaderY + hfh + t2h + subtitle_h + kHeaderBottomGap;
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. draw_bottom_: draws the battery bar and navigation-button hint glyphs.
//    Returns bottom_h = pixels reserved at the bottom (list must stay above).
// ─────────────────────────────────────────────────────────────────────────────
int ListMenuScreen::draw_bottom_(DrawBuffer& buf, int W, int H, std::optional<uint8_t> battery_pct) const {
  if (battery_pct.has_value()) {
    const int bat_pct = battery_pct.value();
    const int kBarW = 26;
    const int kBarH = 8;
    const int kBarX = (W - kBarW) / 2;
    const int kBarY = H - kHintCenterY - kBarH / 2;  // centre the bar on the hint line

    // Outline: rounded corners.
    buf.fill_rect(kBarX + 1, kBarY, kBarW - 2, 1, false);
    buf.fill_rect(kBarX + 1, kBarY + kBarH - 1, kBarW - 2, 1, false);
    buf.fill_rect(kBarX, kBarY + 1, 1, kBarH - 2, false);
    buf.fill_rect(kBarX + kBarW - 1, kBarY + 1, 1, kBarH - 2, false);

    // Fill bar: sloped right edge (fuller = wider).
    const int max_fill = kBarW - 4;
    const int filled = (bat_pct * max_fill) / 100;
    if (filled > 0) {
      buf.fill_row(kBarY + 5, kBarX + 2, kBarX + 2 + std::min(filled + 3, max_fill), false);
      buf.fill_row(kBarY + 4, kBarX + 2, kBarX + 2 + std::min(filled + 2, max_fill), false);
      buf.fill_row(kBarY + 3, kBarX + 2, kBarX + 2 + std::min(filled + 1, max_fill), false);
      buf.fill_row(kBarY + 2, kBarX + 2, kBarX + 2 + std::min(filled, max_fill), false);
    }
  }

  draw_button_hints_(buf);
  return kBottomAreaH;
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. draw_list_: draws the scrollable item list, scrollbar, and boundary lines.
// ─────────────────────────────────────────────────────────────────────────────
void ListMenuScreen::draw_list_(DrawBuffer& buf, int W, int H, int header_h, int bottom_h) const {
  const int n = count();
  if (!ui_font_.valid())
    return;

  if (n == 0) {
    static const char* kEmpty = "No items";
    const int ew = ui_font_.word_width(kEmpty, std::strlen(kEmpty), FontStyle::Regular);
    buf.draw_text_proportional((W - ew) / 2, H / 2 + ui_font_.baseline() - 2, kEmpty, std::strlen(kEmpty), ui_font_,
                               false);
    return;
  }

  // ── Layout metrics ────────────────────────────────────────────────────────
  const int line_h = ui_font_.y_advance() + kItemSpacing;  // height per item slot
  const int baseline = ui_font_.baseline();
  const int available_h = H - header_h - bottom_h;  // bottom_h == kBottomAreaH
  // N items need N*y_advance + (N-1)*kItemSpacing = N*line_h - kItemSpacing pixels.
  // Rearranging: N = (available_h + kItemSpacing) / line_h.
  const int visible = available_h > 0 ? (available_h + kItemSpacing) / line_h : 0;
  const int end = std::min(scroll_offset_ + visible, n);

  // Total pixel height of the currently visible item slots (for centering).
  int total_h = 0;
  for (int i = scroll_offset_; i < end; ++i) {
    if (i < (int)separators_.size() && separators_[i])
      total_h += (i < (int)labels_.size() && !labels_[i].empty()) ? line_h : line_h / 2;
    else
      total_h += line_h;
  }
  // Remove the trailing gap that was counted for the last slot.
  if (total_h >= kItemSpacing)
    total_h -= kItemSpacing;

  // Always vertically centre the visible items in the available space so the
  // gap above the first item and below the last item is equal.
  const int items_y = header_h + (available_h - total_h) / 2;

  // ── Boundary indicator lines ───────────────────────────────────────────────
  // Short centred horizontal lines showing the theoretical top and bottom of the
  // list area, regardless of how many items are currently shown.
  // {
  //   const int ind_w = std::min(80, W / 3);
  //   const int ind_x = (W - ind_w) / 2;
  //   buf.fill_rect(ind_x, header_h, ind_w, 1, false);          // top bound
  //   buf.fill_rect(ind_x, H - bottom_h - 1, ind_w, 1, false);  // bottom bound
  // }

  // ── DEBUG: corner rectangles at screen edges ─────────────────────────────
  // {
  //   const int s = 8;
  //   buf.fill_rect(0, 0, s, s, false);          // top-left
  //   buf.fill_rect(W - s, 0, s, s, false);      // top-right
  //   buf.fill_rect(0, H - s, s, s, false);      // bottom-left
  //   buf.fill_rect(W - s, H - s, s, s, false);  // bottom-right
  // }

  // ── Item rendering ────────────────────────────────────────────────────────
  static const char kEllipsis[] = "...";
  const int ellipsis_w = ui_font_.word_width(kEllipsis, 3, FontStyle::Regular);
  char trunc_buf[260];

  int y = items_y;
  for (int i = scroll_offset_; i < end; ++i) {
    // Separator row
    if (i < (int)separators_.size() && separators_[i]) {
      const std::string_view hdr = get_item_label(i);
      if (!hdr.empty()) {
        const int hw = ui_font_.word_width(hdr.data(), hdr.size(), FontStyle::Regular);
        buf.draw_text_proportional((W - hw) / 2, y + baseline, hdr.data(), hdr.size(), ui_font_, false);
        y += line_h;
      } else {
        y += line_h / 2;
      }
      continue;
    }

    // Regular item
    const std::string_view label_str = get_item_label(i);
    const char* label = label_str.data();
    size_t len = label_str.size();
    int iw = ui_font_.word_width(label, len, FontStyle::Regular);

    const int landscape_pad = (buf.rotation() == Rotation::Deg0) ? 10 : 0;
    const int indent_px = align_left_ ? (32 + ((i < (int)indents_.size() ? indents_[i] : 0) * 20)) : 0;
    const int max_item_w = align_left_ ? (W - 32 - landscape_pad - indent_px) : (W - 48);

    if (iw > max_item_w) {
      const int budget = max_item_w - ellipsis_w;
      size_t fit = 0;
      const char* p = label;
      while (*p) {
        const uint8_t b = static_cast<uint8_t>(*p);
        const size_t cb = b < 0x80 ? 1u : b < 0xE0 ? 2u : b < 0xF0 ? 3u : 4u;
        if (ui_font_.word_width(label, fit + cb, FontStyle::Regular) > budget)
          break;
        fit += cb;
        p += cb;
      }
      const size_t copy = fit < 256 ? fit : 256;
      std::memcpy(trunc_buf, label, copy);
      std::memcpy(trunc_buf + copy, kEllipsis, 3);
      trunc_buf[copy + 3] = '\0';
      label = trunc_buf;
      len = copy + 3;
      iw = ui_font_.word_width(label, len, FontStyle::Regular);
    }

    const int ix = align_left_ ? indent_px : (W - iw) / 2;
    if (i == selected_) {
      const int bar_w = 3;
      const int sel_top = (font_size_idx_ == 0) ? y - 1 : y;
      const int bar_h = ui_font_.y_advance() + (font_size_idx_ == 0 ? 1 : 0);
      if (align_left_) {
        const int bar_x = 16;
        const int bar_width = W - 32 - landscape_pad;
        buf.fill_rect(bar_x + 1, sel_top, bar_width - 2, bar_h, false);
        buf.fill_rect(bar_x, sel_top + 1, 1, bar_h - 2, false);
        buf.fill_rect(bar_x + bar_width - 1, sel_top + 1, 1, bar_h - 2, false);
        buf.draw_text_proportional(ix, y + baseline, label, len, ui_font_, true);
      } else {
        buf.fill_rect(ix - bar_w, sel_top, iw + bar_w * 2, bar_h, false);
        buf.fill_rect(ix - bar_w - 1, sel_top + 1, 1, bar_h - 2, false);
        buf.fill_rect(ix + iw + bar_w, sel_top + 1, 1, bar_h - 2, false);
        buf.draw_text_proportional(ix, y + baseline, label, len, ui_font_, true);
      }
    } else {
      buf.draw_text_proportional(ix, y + baseline, label, len, ui_font_, false);
    }
    y += line_h;
  }

  // ── Scrollbar ─────────────────────────────────────────────────────────────
  if (n > visible) {
    const int sb_w = 4;
    const int sb_x = (buf.rotation() == Rotation::Deg0) ? 8 : W - 12;
    const int sb_top = items_y - (font_size_idx_ == 0 ? 1 : 0);
    // total_h = N*line_h - kItemSpacing (trailing gap removed), so the bottom
    // pixel of the last item's glyph is exactly items_y + total_h.
    const int sb_bottom = items_y + total_h;
    const int sb_total_h = sb_bottom - sb_top;
    const int thumb_min = 20;
    const int thumb_h = std::max(sb_total_h * visible / n, thumb_min);
    const int track = sb_total_h - thumb_h;
    const int max_scroll = n - visible;
    const int thumb_y = sb_top + (max_scroll > 0 ? track * scroll_offset_ / max_scroll : 0);

    buf.fill_rect(sb_x + 1, thumb_y, sb_w - 2, 1, false);                // top cap
    buf.fill_rect(sb_x, thumb_y + 1, sb_w, thumb_h - 2, false);          // body
    buf.fill_rect(sb_x + 1, thumb_y + thumb_h - 1, sb_w - 2, 1, false);  // bottom cap
  }
}

void ListMenuScreen::draw_button_hints_(DrawBuffer& buf) const {
  if (!ui_font_.valid())
    return;
  const int W = buf.width();
  const int H = buf.height();
  const int baseline = ui_font_.baseline();

  // Four labels: back=◀, select=▶, down=▼, up=▲
  bool inv_menu = app_ && app_->invert_menu_buttons();
  const char* lbl_down = "\xe2\x96\xbc";
  const char* lbl_up = "\xe2\x96\xb2";
  const char* kLabels[4] = {"\xe2\x97\x80", "\xe2\x96\xb6", inv_menu ? lbl_up : lbl_down, inv_menu ? lbl_down : lbl_up};
  static const size_t kLens[4] = {3, 3, 3, 3};

  const bool sideways = buf.rotation() == Rotation::Deg90;
  const int L = sideways ? W : H;
  const int pair0 = L * 163 / 550;
  const int pair1 = L - pair0;
  const int gap = 50;
  const int btns[4] = {pair0 - gap, pair0 + gap, pair1 - gap, pair1 + gap};

  for (int i = 0; i < 4; ++i) {
    const int lw = ui_font_.word_width(kLabels[i], kLens[i], FontStyle::Regular);
    if (!sideways) {
      const int text_x = W - kHintCenterY - lw / 2;
      // In landscape, if derived from portrait by rotating -90 deg,
      // the original left button (X=0) becomes the bottom right button (Y=MAX).
      // So we map i -> 3 - i if we need to reverse the order on the Y axis
      // because pair0 is smaller and pair1 is larger.
      int mapped_i = 3 - i;
      const int text_y = btns[mapped_i] - ui_font_.y_advance() / 2 + baseline;
      buf.draw_text_proportional(text_x, text_y, kLabels[i], kLens[i], ui_font_, false);
    } else {
      // Per-size vertical nudge to keep glyphs visually centred on kHintCenterY.
      static constexpr int kHintGlyphNudge[3] = {1, 0, 0};
      const int nudge = kHintGlyphNudge[std::min(font_size_idx_, 2)];
      const int text_y = H - kHintCenterY - (ui_font_.y_advance() + 1) / 2 + baseline + nudge;
      buf.draw_text_proportional(btns[i] - lw / 2, text_y, kLabels[i], kLens[i], ui_font_, false);
    }
  }
}

void ListMenuScreen::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) {
  buf_ = &buf;
  const int n = count();

  // Helper lambdas for selection movement (skip separators).
  auto move_up = [&]() {
    int next = selected_ > 0 ? selected_ - 1 : n - 1;
    while (next != selected_ && next < (int)separators_.size() && separators_[next])
      next = next > 0 ? next - 1 : n - 1;
    selected_ = next;
    ensure_visible_();
  };
  auto move_down = [&]() {
    int next = selected_ < n - 1 ? selected_ + 1 : 0;
    while (next != selected_ && next < (int)separators_.size() && separators_[next])
      next = next < n - 1 ? next + 1 : 0;
    selected_ = next;
    ensure_visible_();
  };

  bool moved = false;       // selection changed — needs a redraw
  bool needs_draw = false;  // on_select returned true — needs a redraw

  // Track whether a fresh press event arrived this frame for each nav direction.
  bool had_up_press = false;
  bool had_down_press = false;

  bool inv_menu = app_ && app_->invert_menu_buttons();
  // Default (inv_menu=false): Button3=up, Button2=down, Up=up, Down=down.
  Button logical_up_front = inv_menu ? Button::Button2 : Button::Button3;
  Button logical_down_front = inv_menu ? Button::Button3 : Button::Button2;
  Button logical_up_side = Button::Up;
  Button logical_down_side = Button::Down;

  Button btn;
  while (buttons.next_press(btn)) {
    if (btn == logical_up_front || btn == logical_up_side) {
      if (n > 0 && !back_held_) {
        move_up();
        moved = true;
        had_up_press = true;
      }
    } else if (btn == logical_down_front || btn == logical_down_side) {
      if (n > 0 && !back_held_) {
        move_down();
        moved = true;
        had_down_press = true;
      }
    } else {
      switch (btn) {
        case Button::Button0:
          if (long_back_threshold_ > 0) {
            if (!back_held_) {
              back_held_ = true;
              back_hold_frames_ = 0;
            }
          } else {
            // Flush any pending move before back so the screen redraws correctly
            // if on_back() decides to stay.
            if (moved) {
              draw_all_(buf, runtime.battery_percentage());
              buf.refresh();
              moved = false;
            }
            on_back();
            if (app_ && app_->has_pending_transition()) {
              return;
            }
          }
          break;

        case Button::Button1:  // select
          if (n > 0 && selected_ < n && !back_held_) {
            on_select(selected_);
            if (app_ && app_->has_pending_transition()) {
              return;
            }
            needs_draw = true;
          }
          break;

        default:
          break;
      }
    }
  }

  // Hold-down acceleration: when a nav button is held (no fresh press this frame),
  // step size grows by 1 each frame: frame 0 = 1, frame 1 = 2, frame 2 = 3, …
  auto hold_step = [](int frames) -> int { return frames + 1; };

  const bool up_held = !had_up_press && !back_held_ && (buttons.is_down(logical_up_front) || buttons.is_down(logical_up_side));
  const bool down_held = !had_down_press && !back_held_ && (buttons.is_down(logical_down_front) || buttons.is_down(logical_down_side));

  if (up_held && n > 0) {
    const int step = hold_step(hold_frames_up_);
    for (int i = 0; i < step; ++i)
      move_up();
    ++hold_frames_up_;
    moved = true;
  } else if (!had_up_press) {
    hold_frames_up_ = 0;
  }

  if (down_held && n > 0) {
    const int step = hold_step(hold_frames_down_);
    for (int i = 0; i < step; ++i)
      move_down();
    ++hold_frames_down_;
    moved = true;
  } else if (!had_down_press) {
    hold_frames_down_ = 0;
  }

  // Long-back hold tracking: count frames while Button0 is held.
  if (back_held_) {
    if (buttons.is_down(Button::Button0)) {
      ++back_hold_frames_;
      if (back_hold_frames_ >= long_back_threshold_) {
        on_long_back(selected_);
        back_held_ = false;
        back_hold_frames_ = 0;
        return;
      }
    } else {
      // Released before threshold — normal tap
      if (moved) {
        draw_all_(buf, runtime.battery_percentage());
        buf.refresh();
        moved = false;
      }
      on_back();
      if (app_ && app_->has_pending_transition()) {
        back_held_ = false;
        back_hold_frames_ = 0;
        return;
      }
      back_held_ = false;
      back_hold_frames_ = 0;
    }
  }

  if (moved || needs_draw || force_redraw_) {
    draw_all_(buf, runtime.battery_percentage());
    buf.refresh();
    force_redraw_ = false;
  }
}

void ListMenuScreen::on_back() {
  if (app_)
    app_->pop_screen();
}

}  // namespace microreader
