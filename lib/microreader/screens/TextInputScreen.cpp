#include "TextInputScreen.h"

#include <cstring>
#include <string_view>

#include "../Application.h"
#include "ListMenuScreen.h"

namespace microreader {

namespace {

// Rows of 10. The number row stays on every page; '-', '.', '_' and '@' (common
// in passwords) are on the letter pages, every other ASCII symbol on Symbols.
const char* const kRowsLower[4] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm._@"};
const char* const kRowsUpper[4] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM._@"};
const char* const kRowsSymbols[4] = {"1234567890", "!\"#$%&'()*", "+,/:;<=>?[", "\\]^`{|}~-_"};

using UiFont = ListMenuScreen::UiFont;
const BitmapFont& ui(UiFont f) {
  return ListMenuScreen::shared_font(f);
}
int text_w(const BitmapFont& f, std::string_view s) {
  return f.word_width(s.data(), s.size(), FontStyle::Regular);
}
void text_center(DrawBuffer& buf, const BitmapFont& f, int cx, int top, std::string_view s, bool white = false) {
  buf.draw_text_proportional(cx - text_w(f, s) / 2, top + f.baseline(), s.data(), s.size(), f, white);
}
void frame(DrawBuffer& buf, int x, int y, int w, int h) {
  buf.fill_rect(x, y, w, 1, false);
  buf.fill_rect(x, y + h - 1, w, 1, false);
  buf.fill_rect(x, y, 1, h, false);
  buf.fill_rect(x + w - 1, y, 1, h, false);
}

}  // namespace

char TextInputScreen::key_char(Page page, int key) {
  if (key < 0 || key >= kCharKeys)
    return 0;
  const char* const* rows = page == Page::Upper ? kRowsUpper : page == Page::Symbols ? kRowsSymbols : kRowsLower;
  return rows[key / kCols][key % kCols];
}

void TextInputScreen::configure(std::string title, std::string subtitle, std::string initial, size_t max_len) {
  title_ = std::move(title);
  subtitle_ = std::move(subtitle);
  text_ = std::move(initial);
  max_len_ = max_len;
  page_ = Page::Lower;
  key_ = kCols;  // 'q': start on the letters
  has_result_ = false;
}

bool TextInputScreen::take_result(std::string& out) {
  if (!has_result_)
    return false;
  has_result_ = false;
  out = text_;
  return true;
}

void TextInputScreen::start(DrawBuffer& buf, IRuntime& runtime) {
  has_result_ = false;
  draw_(buf, runtime.battery_percentage());
}

void TextInputScreen::move_(int delta) {
  key_ = ((key_ + delta) % kNumKeys + kNumKeys) % kNumKeys;
}

void TextInputScreen::move_row_(int delta) {
  // The special row has 5 keys, each two columns wide.
  int row = key_ < kCharKeys ? key_ / kCols : kCharRows;
  int col = key_ < kCharKeys ? key_ % kCols : (key_ - kCharKeys) * 2;
  row = ((row + delta) % (kCharRows + 1) + (kCharRows + 1)) % (kCharRows + 1);
  key_ = row < kCharRows ? row * kCols + col : kCharKeys + col / 2;
}

void TextInputScreen::press_() {
  if (key_ < kCharKeys) {
    if (text_.size() < max_len_)
      text_.push_back(key_char(page_, key_));
    return;
  }
  switch (key_) {
    case kShift:
      page_ = page_ == Page::Lower ? Page::Upper : Page::Lower;
      break;
    case kSymbols:
      page_ = page_ == Page::Symbols ? Page::Lower : Page::Symbols;
      break;
    case kSpace:
      if (text_.size() < max_len_)
        text_.push_back(' ');
      break;
    case kDelete:
      if (!text_.empty())
        text_.pop_back();
      break;
    case kDone:
      done_ = true;
      break;
    default:
      break;
  }
}

void TextInputScreen::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) {
  const bool inv_menu = app_ && app_->invert_menu_buttons();
  const Button prev_front = inv_menu ? Button::Button2 : Button::Button3;
  const Button next_front = inv_menu ? Button::Button3 : Button::Button2;

  bool changed = false, had_move = false;
  done_ = false;
  Button btn;
  while (buttons.next_press(btn)) {
    if (btn == prev_front) {
      move_(-1);
      changed = had_move = true;
    } else if (btn == next_front) {
      move_(+1);
      changed = had_move = true;
    } else if (btn == Button::Up) {
      move_row_(-1);
      changed = true;
    } else if (btn == Button::Down) {
      move_row_(+1);
      changed = true;
    } else if (btn == Button::Button1) {
      press_();
      changed = true;
      if (done_) {
        has_result_ = true;
        if (app_)
          app_->pop_screen();
        return;
      }
    } else if (btn == Button::Button0) {
      if (text_.empty()) {
        if (app_)
          app_->pop_screen();  // cancel
        return;
      }
      text_.pop_back();
      changed = true;
    }
  }
  if (!had_move) {
    if (buttons.is_down(prev_front)) {
      move_(-1);
      changed = true;
    } else if (buttons.is_down(next_front)) {
      move_(+1);
      changed = true;
    }
  }
  if (changed) {
    draw_(buf, runtime.battery_percentage());
    buf.refresh();
  }
}

void TextInputScreen::draw_(DrawBuffer& buf, std::optional<uint8_t> battery) const {
  const int W = buf.width();
  const bool landscape = W > buf.height();
  const int cx = landscape ? (W - 24) / 2 : W / 2;
  buf.fill(true);

  const BitmapFont& header = ui(UiFont::Header);
  const BitmapFont& large = ui(UiFont::Large);
  const BitmapFont& medium = ui(UiFont::Medium);
  const BitmapFont& small = ui(UiFont::Small);

  int y = landscape ? 10 : 15;
  text_center(buf, header, cx, y, title_);
  y += header.y_advance() + 2;
  if (!subtitle_.empty()) {
    text_center(buf, small, cx, y, subtitle_);
    y += small.y_advance();
  }

  // Text field.
  const int field_w = 440;
  const int field_x = cx - field_w / 2;
  const int field_y = y + (landscape ? 8 : 18);
  const int field_h = large.y_advance() + 14;
  frame(buf, field_x, field_y, field_w, field_h);
  std::string shown = text_;
  // Too long: show the end, so the newest characters stay visible.
  const int avail = field_w - 28;
  while (!shown.empty() && text_w(large, shown) > avail)
    shown.erase(0, 1);
  if (shown.size() < text_.size())
    shown.insert(0, "...");
  const int tx = field_x + 10;
  const int ty = field_y + 7;
  buf.draw_text_proportional(tx, ty + large.baseline(), shown.data(), shown.size(), large, false);
  const int caret_x = tx + text_w(large, shown) + 2;
  buf.fill_rect(caret_x, ty + 2, 2, large.y_advance() - 4, false);

  // Keys.
  const int key_w = 44;
  const int key_h = landscape ? 46 : 54;
  const int gap = 2;
  const int grid_w = kCols * key_w + (kCols - 1) * gap;
  const int gx = cx - grid_w / 2;
  const int gy = field_y + field_h + (landscape ? 10 : 30);
  auto draw_key = [&](int x, int y0, int w, std::string_view label, bool sel) {
    if (sel)
      buf.fill_rect(x, y0, w, key_h, false);
    else
      frame(buf, x, y0, w, key_h);
    const BitmapFont& f = label.size() == 1 ? large : medium;
    text_center(buf, f, x + w / 2, y0 + (key_h - f.y_advance()) / 2, label, sel);
  };
  for (int k = 0; k < kCharKeys; ++k) {
    const char c = key_char(page_, k);
    const int x = gx + (k % kCols) * (key_w + gap);
    const int ky = gy + (k / kCols) * (key_h + gap);
    draw_key(x, ky, key_w, std::string_view(&c, 1), k == key_);
  }
  const char* const special[5] = {page_ == Page::Upper ? "abc" : "ABC", page_ == Page::Symbols ? "abc" : "#+=",
                                  "Space", "Del", "Done"};
  const int sw = 2 * key_w + gap;
  const int sy = gy + kCharRows * (key_h + gap);
  for (int i = 0; i < 5; ++i)
    draw_key(gx + i * (sw + gap), sy, sw, special[i], key_ == kCharKeys + i);

  // Help.
  const int help_y = sy + key_h + (landscape ? 6 : 18);
  text_center(buf, small, cx, help_y, "\xe2\x96\xb2 \xe2\x96\xbc move    side buttons: row up/down");
  text_center(buf, small, cx, help_y + small.y_advance() + 2, "\xe2\x96\xb6 type    \xe2\x97\x80 delete (cancel when empty)");

  ListMenuScreen::draw_menu_bottom_bar(buf, ListMenuScreen::shared_menu_font(), app_ && app_->invert_menu_buttons(),
                                       battery);
}

}  // namespace microreader
