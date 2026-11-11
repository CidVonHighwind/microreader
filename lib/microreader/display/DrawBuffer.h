#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "../content/BitmapFont.h"
#include "../content/TextLayout.h"
#include "ui_font_small.h"

#ifdef ESP_PLATFORM
#include "asset_blob.h"
#endif

namespace microreader {

enum class Rotation { Deg0 = 0, Deg90 = 90 };

// Refresh mode for full-screen updates.
enum class RefreshMode { Full, Half };

// Which bitmap plane to read from a GlyphData during grayscale rendering.
enum class GrayPlane { BW, LSB, MSB };

// Physical screen constants and bit-packed pixel helpers (used internally by DrawBuffer).
// Two devices share this firmware; the X3 build defines MICROREADER_X3.
struct DisplayFrame {
#ifdef MICROREADER_X3
  // Xteink X3: 792x528 panel (UC8253/UC8279 controller), every column visible.
  static constexpr int kPhysicalWidth = 792;  // visible app-space width
  static constexpr int kPanelOffsetX = 0;     // no hidden columns
  static constexpr int kPanelWidth = 792;
  static constexpr int kPhysicalHeight = 528;
#else
  // Xteink X4: 800x480 panel (SSD1677), hidden area at the panel edges.
  static constexpr int kPhysicalWidth = 786;  // visible app-space width (panel has hidden area at the top and bottom)
  static constexpr int kPanelOffsetX = 10;    // hidden columns at the left edge of the 800px panel
  static constexpr int kPanelWidth = 800;
  static constexpr int kPhysicalHeight = 480;
#endif
  static constexpr int kStride = kPanelWidth / 8;  // bytes per row (panel width divisible by 8)
  static constexpr std::size_t kPixelBytes = static_cast<std::size_t>(kStride) * kPhysicalHeight;
};

// Display driver interface implemented by each platform.
class IDisplay {
 public:
  virtual ~IDisplay() = default;

  // Full physical refresh. Both BW and RED RAM are set to `pixels`.
  virtual void full_refresh(const uint8_t* pixels, RefreshMode mode, bool turnOffScreen = false) = 0;

  // Partial refresh: new_pixels -> BW RAM. Driver tracks previous frame for RED RAM.
  // prev_pixels is the previous BW frame (used to restore BW RAM after grayscale revert).
  virtual void partial_refresh(const uint8_t* new_pixels, const uint8_t* prev_pixels) = 0;

  // Write data to BW RAM only (no refresh). Used for grayscale LSB plane.
  virtual void write_ram_bw(const uint8_t* data) {
    (void)data;
  }

  // Write data to RED RAM only (no refresh). Used for grayscale MSB plane.
  virtual void write_ram_red(const uint8_t* data) {
    (void)data;
  }

  // Trigger a grayscale display refresh using the multi-pass anti-aliasing LUT (kLutGrayscale).
  // Assumes BW RAM and RED RAM have already been written via write_ram_bw/write_ram_red.
  virtual void grayscale_refresh(bool turnOffScreen = false) {}

  // Trigger a one-pass grayscale refresh using kLutFactoryQuality.
  // RAM encoding: state = (red_bit << 1) | bw_bit; 0=white, 1=light, 2=dark, 3=black.
  virtual void grayscale_refresh_1pass(bool turnOffScreen = false) {}

  // Revert grayscale overlay and restore prev_pixels into RED RAM.
  // Must be called while the buffer holding the pre-grayscale BW frame is still valid.
  virtual void revert_grayscale(const uint8_t* prev_pixels) {
    (void)prev_pixels;
  }

  // Partially refresh a physical sub-rectangle of the display.
  // new_buf: 1-bit packed, row-major, stride_bytes per row.
  // phys_x is a raw hardware column — caller must add DisplayFrame::kPanelOffsetX and
  // ensure the result is byte-aligned (multiple of 8).
  // Default: no-op - platforms may override for efficient region updates.
  virtual void partial_refresh_region(int phys_x, int phys_y, int phys_w, int phys_h, const uint8_t* new_buf,
                                      int stride_bytes) {
    (void)phys_x;
    (void)phys_y;
    (void)phys_w;
    (void)phys_h;
    (void)new_buf;
    (void)stride_bytes;
  }

  // Put the display controller into deep sleep (low-power mode).
  virtual void deep_sleep() {}

  // Notify the display of the logical rotation used by the caller.
  // Used by the desktop emulator to resize/orient the SDL window.
  virtual void set_rotation(Rotation r) {
    (void)r;
  }

  // Query if display is currently in grayscale mode.
  virtual bool in_grayscale_mode() const {
    return false;
  }

  // Returns true if the display hardware is currently busy refreshing.
  // Default: always ready. Platforms may override for non-blocking region updates.
  virtual bool is_busy() const {
    return false;
  }

  // True for UC-style controllers (the X3), whose 4-level grayscale is a tint
  // pass over an already-displayed black & white frame rather than the
  // SSD1677's two bit-planes. It changes what the staging calls carry:
  // write_ram_bw() gets the "dark gray" mask, write_ram_red() the "any gray"
  // mask, and the caller shows the B&W frame first.
  virtual bool uc_grayscale() const {
    return false;
  }
};

// Double-buffered display with simple draw helpers.
//
// Uses Deg90 (portrait) rotation: logical 480x788, physical 788x480.
// The "inactive" buffer is drawn to; "active" is what's currently displayed.
// refresh() swaps and does a partial hardware refresh.
//
// Scratch buffer loan: scratch_buf1()/scratch_buf2() expose both ~48KB buffers
// for callers (e.g. EPUB->MRB conversion). Call reset_after_scratch() when done.
class DrawBuffer {
 public:
  // Logical portrait dimensions.
  static constexpr int kWidth = DisplayFrame::kPhysicalHeight;
  static constexpr int kHeight = DisplayFrame::kPhysicalWidth;
  static constexpr size_t kBufSize = DisplayFrame::kPixelBytes;

  explicit DrawBuffer(IDisplay& display) : display_(display) {
    memset(bufs_[0], 0xFF, kBufSize);
    memset(bufs_[1], 0xFF, kBufSize);
    set_rotation(Rotation::Deg90);
  }

  IDisplay& display() {
    return display_;
  }
  const IDisplay& display() const {
    return display_;
  }

  // Set logical rotation; updates both the display driver and the draw-transform in DrawBuffer.
  void set_rotation(Rotation r) {
    rotation_ = r;
    display_.set_rotation(r);
  }

  // Override only the local coordinate transform without touching the display driver.
  // Use to draw UI elements in a fixed orientation regardless of current rotation.
  // Restore with set_rotation_transform(rotation()) before the next display commit.
  void set_rotation_transform(Rotation r) {
    rotation_ = r;
  }

  // Runtime logical dimensions (depend on rotation).
  int width() const {
    return rotation_ == Rotation::Deg0 ? DisplayFrame::kPhysicalWidth : kWidth;
  }
  int height() const {
    return rotation_ == Rotation::Deg0 ? DisplayFrame::kPhysicalHeight : kHeight;
  }

  Rotation rotation() const {
    return rotation_;
  }

  // -- Draw helpers (logical coordinates)
  // ----------------------------------------

  // Fill the entire inactive buffer.
  void fill(bool white = true) {
    memset(inactive_(), white ? 0xFF : 0x00, kBufSize);
  }

  // Fill a logical rectangle.
  void fill_rect(int lx, int ly, int lw, int lh, bool white) {
    if (rotation_ == Rotation::Deg0)
      fill_rect_physical_(full_target_(), lx, ly, lw, lh, white);
    else
      fill_rect_physical_(full_target_(), ly, DisplayFrame::kPhysicalHeight - lx - lw, lh, lw, white);
  }

  // Fill a logical horizontal span [x1, x2) on logical row ly.
  void fill_row(int ly, int x1, int x2, bool white) {
    if (rotation_ == Rotation::Deg0) {
      x1 = std::max(x1, 0);
      x2 = std::min(x2, DisplayFrame::kPhysicalWidth);
      if (x1 >= x2 || ly < 0 || ly >= DisplayFrame::kPhysicalHeight)
        return;
      fill_row_physical_(full_target_(), ly, x1, x2, white);
    } else {
      x1 = std::max(x1, 0);
      x2 = std::min(x2, kWidth);
      if (x1 >= x2 || ly < 0 || ly >= kHeight)
        return;
      // Deg90: logical row ly -> physical col ly; logical cols [x1,x2) -> physical rows [PhysH-x2, PhysH-x1)
      fill_col_physical_(full_target_(), ly, DisplayFrame::kPhysicalHeight - x2, DisplayFrame::kPhysicalHeight - x1,
                         white);
    }
  }

  // Blit a 1-bit packed image into the inactive buffer at physical position (x, y).
  // Coordinates are physical (not logical). Clips to display bounds.
  void draw_image(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!imageData || w == 0 || h == 0)
      return;

    if (x >= DisplayFrame::kPhysicalWidth || y >= DisplayFrame::kPhysicalHeight)
      return;

    const uint16_t imageWidthBytes = static_cast<uint16_t>((w + 7) / 8);
    const uint16_t max_width = static_cast<uint16_t>(DisplayFrame::kPhysicalWidth - x);
    const uint16_t draw_width = std::min<uint16_t>(w, max_width);
    const uint16_t draw_bytes = static_cast<uint16_t>((draw_width + 7) / 8);
    uint8_t* buf = inactive_();

    // Add panel offset so app-space x=0 maps to buffer column 12.
    const uint16_t x_buf = static_cast<uint16_t>(x + DisplayFrame::kPanelOffsetX);
    const uint16_t dest_offset_x = static_cast<uint16_t>(x_buf / 8);
    const uint8_t bit_offset = static_cast<uint8_t>(x_buf & 7);

    auto set_pixel_physical = [&](uint16_t px, uint16_t py, bool white) {
      if (px >= DisplayFrame::kPhysicalWidth || py >= DisplayFrame::kPhysicalHeight)
        return;
      const uint16_t px_buf = static_cast<uint16_t>(px + DisplayFrame::kPanelOffsetX);
      size_t idx = static_cast<size_t>(py) * DisplayFrame::kStride + (px_buf / 8);
      uint8_t bit = static_cast<uint8_t>(0x80u >> (px_buf & 7));
      if (white)
        buf[idx] |= bit;
      else
        buf[idx] &= static_cast<uint8_t>(~bit);
    };

    for (uint16_t row = 0; row < h; ++row) {
      uint16_t destY = y + row;
      if (destY >= DisplayFrame::kPhysicalHeight)
        break;

      const size_t destRowStart = static_cast<size_t>(destY) * DisplayFrame::kStride + dest_offset_x;
      const size_t srcRowStart = static_cast<size_t>(row) * imageWidthBytes;

      if (bit_offset == 0 && (w & 7) == 0) {
        const uint16_t copy_bytes = std::min<uint16_t>(imageWidthBytes, draw_bytes);
        memcpy(buf + destRowStart, imageData + srcRowStart, copy_bytes);
      } else {
        for (uint16_t col = 0; col < draw_width; ++col) {
          const size_t src_byte = srcRowStart + (col / 8);
          const uint8_t src_bit = static_cast<uint8_t>((imageData[src_byte] >> (7 - (col & 7))) & 1);
          set_pixel_physical(static_cast<uint16_t>(x + col), destY, src_bit != 0);
        }
      }
    }
  }

  // Set a single logical pixel.
  void set_pixel(int lx, int ly, bool white) {
    int px, py;
    if (rotation_ == Rotation::Deg0) {
      if (lx < 0 || lx >= DisplayFrame::kPhysicalWidth || ly < 0 || ly >= DisplayFrame::kPhysicalHeight)
        return;
      px = lx;
      py = ly;
    } else {
      if (lx < 0 || lx >= kWidth || ly < 0 || ly >= kHeight)
        return;
      px = ly;
      py = DisplayFrame::kPhysicalHeight - 1 - lx;
    }
    uint8_t* buf = inactive_();
    const int px_buf = px + DisplayFrame::kPanelOffsetX;
    const size_t bidx = static_cast<size_t>(py * DisplayFrame::kStride + px_buf / 8);
    const uint8_t bit = static_cast<uint8_t>(0x80u >> (px_buf & 7));
    if (white)
      buf[bidx] |= bit;
    else
      buf[bidx] &= static_cast<uint8_t>(~bit);
  }

  // Blit a horizontal row of 1-bit packed pixels at logical position (lx, ly).
  // data_1bit is MSB-first packed, width pixels long.
  void blit_1bit_row(int lx, int ly, const uint8_t* data_1bit, int width) {
    uint8_t* buf = inactive_();
    if (rotation_ == Rotation::Deg0) {
      if (ly < 0 || ly >= DisplayFrame::kPhysicalHeight || width <= 0)
        return;
      int col_start = 0, col_end = width;
      if (lx < 0)
        col_start = -lx;
      if (lx + width > DisplayFrame::kPhysicalWidth)
        col_end = DisplayFrame::kPhysicalWidth - lx;
      for (int col = col_start; col < col_end; ++col) {
        const int px = lx + col;
        const int px_buf = px + DisplayFrame::kPanelOffsetX;
        const size_t bidx = static_cast<size_t>(ly) * DisplayFrame::kStride + px_buf / 8;
        const uint8_t set_mask = static_cast<uint8_t>(0x80u >> (px_buf & 7));
        const uint8_t clr_mask = static_cast<uint8_t>(~set_mask);
        const bool white = (data_1bit[col >> 3] >> (7 - (col & 7))) & 1;
        if (white)
          buf[bidx] |= set_mask;
        else
          buf[bidx] &= clr_mask;
      }
    } else {
      if (ly < 0 || ly >= kHeight || width <= 0)
        return;
      const int px = ly;
      const int px_buf = px + DisplayFrame::kPanelOffsetX;
      const int byte_col = px_buf / 8;
      const uint8_t set_mask = static_cast<uint8_t>(0x80u >> (px_buf & 7));
      const uint8_t clr_mask = static_cast<uint8_t>(~set_mask);
      // Clip x range to [0, kWidth)
      int col_start = 0, col_end = width;
      if (lx < 0)
        col_start = -lx;
      if (lx + width > kWidth)
        col_end = kWidth - lx;
      for (int col = col_start; col < col_end; ++col) {
        const int py = DisplayFrame::kPhysicalHeight - 1 - (lx + col);
        const size_t bidx = static_cast<size_t>(py) * DisplayFrame::kStride + byte_col;
        const bool white = (data_1bit[col >> 3] >> (7 - (col & 7))) & 1;
        if (white)
          buf[bidx] |= set_mask;
        else
          buf[bidx] &= clr_mask;
      }
    }
  }

  // Draw a filled circle.
  void draw_circle(int cx, int cy, int r, bool white) {
    if (r <= 0)
      return;
    const int r2 = r * r;
    int dx = r;
    for (int dy = 0; dy <= r; ++dy) {
      while (dx * dx + dy * dy > r2)
        --dx;
      if (dx < 0)
        break;
      fill_row(cy + dy, cx - dx, cx + dx + 1, white);
      if (dy != 0)
        fill_row(cy - dy, cx - dx, cx + dx + 1, white);
    }
  }

  // -- Text & Glyph rendering --

  // Draw text with the UI font, including background fill.
  // white=true  -> white background, black glyphs (normal unselected item).
  // white=false -> black background, white glyphs (highlighted selected item).
  // The scale parameter is accepted for API compatibility but ignored.
  void draw_text(int x, int y, const char* text, bool white, int /*scale*/ = 1) {
    if (!text || !*text)
      return;
    const BitmapFont& f = ui_font_();
    const int w = static_cast<int>(f.word_width(text, strlen(text), FontStyle::Regular));
    const int h = static_cast<int>(f.glyph_height());
    fill_rect(x, y, w, h, white);
    draw_text_proportional(x, y + static_cast<int>(f.baseline()), text, f, !white);
  }

  // Draw text centered at (cx, y) using the UI font.
  // fill_bg=true (default): fills a background rect behind the text.
  // fill_bg=false: only paints text pixels, leaving the background untouched.
  void draw_text_centered(int cx, int y, const char* text, bool white, bool fill_bg = true) {
    if (!text || !*text)
      return;
    const BitmapFont& f = ui_font_();
    const int w = static_cast<int>(f.word_width(text, strlen(text), FontStyle::Regular));
    if (fill_bg)
      draw_text(cx - w / 2, y, text, white);
    else
      draw_text_no_bg(cx - w / 2, y, text, !white);
  }

  void draw_text_no_bg(int x, int y, const char* text, bool white, int /*scale*/ = 1) {
    if (!text || !*text)
      return;
    const BitmapFont& f = ui_font_();
    draw_text_proportional(x, y + static_cast<int>(f.baseline()), text, f, white);
  }

  // Draw proportional text using a BitmapFont. Cursor starts at (x, baseline_y)
  // where baseline_y is the Y position of the text baseline.
  // Returns the X position after the last character (cursor advance).
  int draw_text_proportional(int x, int baseline_y, const char* text, size_t len, const BitmapFont& font, bool white,
                             FontStyle style = FontStyle::Regular);

  // Convenience overload for null-terminated strings.
  int draw_text_proportional(int x, int baseline_y, const char* text, const BitmapFont& font, bool white,
                             FontStyle style = FontStyle::Regular) {
    return draw_text_proportional(x, baseline_y, text, text ? strlen(text) : 0, font, white, style);
  }

  // -- Plane-aware text rendering (for grayscale two-pass) -----------------

  // Render text from a specific grayscale plane into an explicit buffer.
  int draw_text_plane(uint8_t* buf, int x, int baseline_y, const char* text, size_t len, const BitmapFontSet& fonts,
                      GrayPlane plane, bool white, FontStyle style = FontStyle::Regular, uint8_t size_pct = 100);

  // Render all words of a LayoutLine and — on the BW plane only — draw continuous
  // underline decorations spanning each run of consecutive linked words. This is the
  // correct level of abstraction: underlines are line decorations, not per-word.
  void draw_layout_line(uint8_t* buf, int x_offset, int baseline_y, const PageLine& line, const BitmapFontSet& fonts,
                        GrayPlane plane, bool white);

  // -- Grayscale display operations
  // -------------------------------------

  // Write the inactive buffer to BW RAM only (no refresh).
  bool uc_grayscale() const {
    return display_.uc_grayscale();
  }

  void write_ram_bw() {
    display_.write_ram_bw(inactive_());
  }

  // Draw text glyphs only (no background fill). Glyph color = white param.
  // The scale parameter is accepted for API compatibility but ignored.

  // Write the inactive buffer to RED RAM only (no refresh).
  void write_ram_red() {
    display_.write_ram_red(inactive_());
  }

  // Trigger grayscale refresh (assumes BW/RED RAM already written).
  void grayscale_refresh(bool turnOffScreen = false) {
    display_.grayscale_refresh(turnOffScreen);
  }

  // Revert grayscale using the active (displayed) buffer as prev_pixels.
  void revert_grayscale() {
    display_.revert_grayscale(active_());
  }

  // Provide direct access to the inactive buffer for multi-pass rendering.
  uint8_t* render_buf() {
    return inactive_();
  }

  // -- Display operations
  // --------------------------------------------------

  // Swap active<->inactive, then do a partial hardware refresh.
  void refresh() {
    display_.partial_refresh(inactive_(), active_());
    active_idx_ = 1 - active_idx_;
    active_valid_ = true;
  }

  // Call full hardware refresh using the current inactive buffer, then sync both.
  void full_refresh(RefreshMode mode = RefreshMode::Half, bool turnOffScreen = false) {
    display_.full_refresh(inactive_(), mode, turnOffScreen);
    memcpy(bufs_[active_idx_], bufs_[1 - active_idx_], kBufSize);
    active_idx_ = 1 - active_idx_;
    active_valid_ = true;
  }

  // Put the display into deep sleep (low-power mode). Call after a full refresh.
  void deep_sleep() {
    display_.deep_sleep();
  }

  // Write the active (currently-displayed) buffer to BW RAM so that BW and RED
  // RAM are in sync before a grayscale pass. No-op if active buffer is stale.
  void sync_bw_ram() {
    if (active_valid_)
      display_.write_ram_bw(active_());
  }

  // Write pre-built LSB/MSB plane arrays to display RAM, trigger a grayscale
  // refresh with screen power-off, then deep sleep. Intended for the power-off splash screen.
  // Uses draw_image() so that images wider than kPhysicalWidth (e.g. 800px) are clipped correctly.
  void show_grayscale_image(const uint8_t* lsb, const uint8_t* msb, uint16_t w, uint16_t h) {
    fill(true);
    draw_image(lsb, 0, 0, w, h);
    display_.write_ram_bw(inactive_());
    fill(true);
    draw_image(msb, 0, 0, w, h);
    display_.write_ram_red(inactive_());
    display_.grayscale_refresh(/*turnOffScreen=*/true);
    display_.deep_sleep();
  }

  // Load and show sleep image from MGR2 file (2bpp, 4 gray levels).
  // state = (RED_bit << 1) | BW_bit; 0=black, 1=dark gray, 2=light gray, 3=white.
  bool show_sleep_image(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f)
      return false;
    Mgr2Source_ src = Mgr2Source_::from_file(f);
    if (src.valid())
      show_mgr2_sleep_(src, false);
    std::fclose(f);
    return src.valid();
  }

  bool show_sleep_image_embedded(int idx = 0) {
    Mgr2Source_ src;

#ifdef ESP_PLATFORM
    const char* name = (idx == 1) ? "sleep_1.mgr" : (idx == 2) ? "sleep_2.mgr" : "sleep_0.mgr";
    size_t size = 0;
    esp_partition_mmap_handle_t mmap_h = 0;
    const uint8_t* data = static_cast<const uint8_t*>(asset_blob::g_assets.map(name, size, mmap_h));
    if (!data)
      return false;
    src = Mgr2Source_::from_memory(data, size);
#else
    char path[64];
    snprintf(path, sizeof(path), "resources/sleep/sleep_%d.mgr", idx);
    FILE* f = std::fopen(path, "rb");
    if (!f)
      return false;
    src = Mgr2Source_::from_file(f);
#endif

    if (src.valid())
      show_mgr2_sleep_(src, true);

#ifdef ESP_PLATFORM
    asset_blob::g_assets.unmap(mmap_h);
#else
    std::fclose(f);
#endif
    return src.valid();
  }

  // -- Scratch buffer loan (for EPUB conversion / image decode) ------------

  // Loan the inactive buffer as scratch (will be overwritten before next refresh).
  uint8_t* scratch_buf1() {
    return bufs_[1 - active_idx_];
  }
  // Loan the active buffer as scratch (for operations needing two buffers).
  uint8_t* scratch_buf2() {
    return bufs_[active_idx_];
  }

  // Reset both buffers after scratch use, before drawing new content.
  // Marks the active buffer as no longer reflecting the display, so sync_bw_ram()
  // becomes a no-op until the next refresh().
  void reset_after_scratch(bool white = true) {
    memset(bufs_[0], white ? 0xFF : 0x00, kBufSize);
    memset(bufs_[1], white ? 0xFF : 0x00, kBufSize);
    active_idx_ = 0;
    active_valid_ = false;
  }

  // -- Loading box region update
  // --------------------------------------------
  //
  // A small fixed region at the bottom-centre of the logical screen, used to
  // show a "Converting..." indicator while both display buffers are in use as
  // scratch.  The region is byte-aligned in physical space so the extraction
  // can be done with plain memcpy (no bit-shifting).
  //
  // Logical box: 256 x 32 px, centred horizontally, flush to the bottom.
  //   lx = (480 - 256) / 2 = 112,  ly = 788 - 32 = 756
  // Physical (Deg90 rotation):
  //   px = kLoadPhysX = 750  (byte-aligned: 750 + kPanelOffsetX(10) = 760 = 95*8)
  //   py = PhysH - lx - lw = 480 - 112 - 256 = 112
  //   pw = lh = 32   ->  stride = 4 bytes
  //   ph = lw = 256
  // Mini-buffer size: 4 x 256 = 1024 bytes each (stack-allocated).

  static constexpr int kLoadLogW = 256;
  static constexpr int kLoadLogH = 32;
  static constexpr int kLoadLogX = (kWidth - kLoadLogW) / 2;  // 112
  static constexpr int kLoadLogY = kHeight - kLoadLogH;       // - 4;   // 744

  // kLoadPhysX must satisfy: (kLoadPhysX + kPanelOffsetX) % 8 == 0 for byte-aligned hardware writes.
  // With kPanelOffsetX=10: 750+10=760 ✓  (box ends at 782, 4px from logical bottom edge 786).
  // Largest such value that keeps the box on the panel (750 on the X4, 760 on the X3).
  static constexpr int kLoadPhysX =
      (DisplayFrame::kPhysicalWidth - kLoadLogH + DisplayFrame::kPanelOffsetX) / 8 * 8 - DisplayFrame::kPanelOffsetX;
  static constexpr int kLoadPhysY = DisplayFrame::kPhysicalHeight - kLoadLogX - kLoadLogW;  // 112
  static constexpr int kLoadPhysW = kLoadLogH;                                              // 32
  static constexpr int kLoadPhysH = kLoadLogW;                                              // 256
  static constexpr int kLoadStride = (kLoadPhysW + 7) / 8;                                  // 4
  static constexpr int kLoadBufBytes = kLoadStride * kLoadPhysH;                            // 1024

  // Bar geometry.
  static constexpr int kBarW = 160;
  static constexpr int kBarH = 7;
  static constexpr int kBarX = kWidth / 2 - kBarW / 2;
  static constexpr int kBarY = kLoadLogY + kLoadLogH - kBarH - 4;

  // Draw a loading box (label + progress bar) and push it to the display
  // via a region-only hardware update.  The main draw buffers are NEVER
  // touched - all rendering goes directly into the 1280-byte mini-buffer.
  // Both display buffers may be used as scratch before or after this call.
  //
  //   text         - label shown inside the box (e.g. "Converting...")
  //   progress_pct - 0-100; controls how much of the bar is filled
  void show_loading(const char* text, int progress_pct) {
    if (display_.is_busy())
      return;  // skip if panel is still refreshing
    uint8_t new_buf[kLoadBufBytes];
    render_loading_box_(new_buf, text, progress_pct);
    display_.partial_refresh_region(kLoadPhysX + DisplayFrame::kPanelOffsetX, kLoadPhysY, kLoadPhysW, kLoadPhysH,
                                    new_buf, kLoadStride);
  }

 private:
  // Describes a render target: a pixel buffer with its own stride and physical offset/bounds.
  // phys_x0 is used for clipping (app-space coordinates).
  // buf_x0  is the app-space X that maps to buffer column 0 (for byte-index: col = x - buf_x0).
  //   Full-frame buffer: buf_x0 = -kPanelOffsetX  (app X=0 → buffer column 12)
  //   Mini loading-box:  buf_x0 = phys_x0         (local buffer, no panel offset)
  struct RenderTarget {
    uint8_t* buf;
    int stride;   // bytes per physical row
    int phys_x0;  // absolute physical X of the left edge for clipping (app space)
    int phys_y0;  // absolute physical Y of the top edge
    int phys_w;   // width in pixels for clipping
    int phys_h;   // height in rows
    int buf_x0;   // app-space X that maps to buffer column 0
  };

  static void draw_glyph_impl_(const RenderTarget& t, int x, int y, const uint8_t* bits, int bitmap_width,
                               int bitmap_height, int x_offset, int y_offset, bool white, bool invert_select = false,
                               Rotation rotation = Rotation::Deg90) {
    if (!bits || bitmap_width <= 0 || bitmap_height <= 0)
      return;
    const int gx = x + x_offset;
    const int gy = y + y_offset;
    const int row_stride = (bitmap_width + 7) / 8;

    if (rotation == Rotation::Deg90) {
      // Optimized transposed path for portrait (Deg90) — the common case.
      //
      // Physical mapping: px = gy + row,  py = kPhysicalHeight-1 - gx - col
      //
      // Transposed loop: outer=col (selects one physical row → sequential memory),
      // inner=row (writes adjacent bits within that row).
      // Batched inner loop packs 8 rows into one output byte for 8× fewer
      // read-modify-write ops over the cache-friendly forward scan.
      const int lpy_base = DisplayFrame::kPhysicalHeight - 1 - gx - t.phys_y0;
      // col range where py_local = lpy_base - col is in [0, phys_h):
      const int col_start = (lpy_base >= t.phys_h) ? lpy_base - (t.phys_h - 1) : 0;
      const int col_end = (lpy_base >= 0) ? (lpy_base < bitmap_width ? lpy_base + 1 : bitmap_width) : 0;
      if (col_start >= col_end)
        return;
      // row range where px_local = lpx0 + row is in [0, phys_w):
      const int lpx0_clip = gy - t.phys_x0;
      const int lpx0 = gy - t.buf_x0;
      const int row_lo = (lpx0_clip < 0) ? -lpx0_clip : 0;
      const int row_hi = (lpx0_clip + bitmap_height > t.phys_w) ? t.phys_w - lpx0_clip : bitmap_height;
      if (row_lo >= row_hi)
        return;

      for (int col = col_start; col < col_end; ++col) {
        uint8_t* const row_ptr = t.buf + static_cast<size_t>(lpy_base - col) * static_cast<size_t>(t.stride);
        const int src_byte_off = col >> 3;
        const int src_shift = 7 - (col & 7);

        int row = row_lo;

        // Scalar prefix: advance until (lpx0 + row) is on a byte boundary.
        const int align_end_raw = row_lo + ((8 - ((lpx0 + row_lo) & 7)) & 7);
        const int prefix_end = align_end_raw < row_hi ? align_end_raw : row_hi;
        for (; row < prefix_end; ++row) {
          const uint8_t glyph_bit = static_cast<uint8_t>((bits[row * row_stride + src_byte_off] >> src_shift) & 1u);
          if (invert_select ? glyph_bit : !glyph_bit) {
            const int px_local = lpx0 + row;
            const uint8_t mask = static_cast<uint8_t>(0x80u >> (px_local & 7));
            if (white)
              row_ptr[px_local >> 3] |= mask;
            else
              row_ptr[px_local >> 3] &= static_cast<uint8_t>(~mask);
          }
        }

        // Batched inner loop: 8 source rows → 1 output byte (byte-aligned in framebuffer).
        for (; row + 8 <= row_hi; row += 8) {
          const int out_idx = (lpx0 + row) >> 3;  // byte-aligned: (lpx0+row) & 7 == 0
          const uint8_t* src = bits + row * row_stride + src_byte_off;
          // Pack column bits from 8 successive rows into one byte (MSB = row+0).
          uint8_t col_byte;
          col_byte = static_cast<uint8_t>((*src >> src_shift) & 1u) << 7;
          src += row_stride;
          col_byte |= static_cast<uint8_t>((*src >> src_shift) & 1u) << 6;
          src += row_stride;
          col_byte |= static_cast<uint8_t>((*src >> src_shift) & 1u) << 5;
          src += row_stride;
          col_byte |= static_cast<uint8_t>((*src >> src_shift) & 1u) << 4;
          src += row_stride;
          col_byte |= static_cast<uint8_t>((*src >> src_shift) & 1u) << 3;
          src += row_stride;
          col_byte |= static_cast<uint8_t>((*src >> src_shift) & 1u) << 2;
          src += row_stride;
          col_byte |= static_cast<uint8_t>((*src >> src_shift) & 1u) << 1;
          src += row_stride;
          col_byte |= static_cast<uint8_t>((*src >> src_shift) & 1u);
          // col_byte bit (7-b) = glyph source bit for row+b. 1=background, 0=ink (normal).
          const uint8_t ink = invert_select ? col_byte : static_cast<uint8_t>(~col_byte);
          if (white)
            row_ptr[out_idx] |= ink;
          else
            row_ptr[out_idx] &= static_cast<uint8_t>(~ink);
        }

        // Scalar tail.
        for (; row < row_hi; ++row) {
          const uint8_t glyph_bit = static_cast<uint8_t>((bits[row * row_stride + src_byte_off] >> src_shift) & 1u);
          if (invert_select ? glyph_bit : !glyph_bit) {
            const int px_local = lpx0 + row;
            const uint8_t mask = static_cast<uint8_t>(0x80u >> (px_local & 7));
            if (white)
              row_ptr[px_local >> 3] |= mask;
            else
              row_ptr[px_local >> 3] &= static_cast<uint8_t>(~mask);
          }
        }
      }
    } else {
      // Deg0 (landscape) — general path.
      for (int row = 0; row < bitmap_height; ++row) {
        const int ly = gy + row;
        const uint8_t* row_data = bits + row * row_stride;
        for (int col = 0; col < bitmap_width; ++col) {
          const int lx = gx + col;
          const bool bit_set = (row_data[col >> 3] >> (7 - (col & 7))) & 1;
          if (invert_select ? bit_set : !bit_set) {
            const int px = lx;
            const int py = ly;
            if (px < t.phys_x0 || px >= t.phys_x0 + t.phys_w)
              continue;
            if (py < t.phys_y0 || py >= t.phys_y0 + t.phys_h)
              continue;
            const int lpx = px - t.buf_x0;
            const int lpy = py - t.phys_y0;
            const size_t bidx = static_cast<size_t>(lpy * t.stride + lpx / 8);
            const uint8_t bit = static_cast<uint8_t>(0x80u >> (lpx & 7));
            if (white)
              t.buf[bidx] |= bit;
            else
              t.buf[bidx] &= static_cast<uint8_t>(~bit);
          }
        }
      }
    }
  }

  IDisplay& display_;
  alignas(4) uint8_t bufs_[2][kBufSize];
  int active_idx_ = 0;
  bool active_valid_ = true;  // false after reset_after_scratch(); restored by refresh()/full_refresh()
  Rotation rotation_ = Rotation::Deg90;

  uint8_t* inactive_() {
    return bufs_[1 - active_idx_];
  }
  const uint8_t* active_() const {
    return bufs_[active_idx_];
  }

  // Returns the shared UI font backed by ui_font_small.h data.
  static const BitmapFont& ui_font_() {
    static BitmapFont font(kFontData_ui_small_mbf, sizeof(kFontData_ui_small_mbf));
    return font;
  }

  // Unified source for MGR2 sleep images — wraps either a file or a memory blob.
  // Parses the 8-byte MGR2 header on construction; get_row() returns a pointer to
  // the 2bpp data for any given row (reads from file into row_buf_ or returns a
  // direct pointer into the memory blob).
  struct Mgr2Source_ {
    uint16_t w = 0, h = 0;
    size_t src_stride = 0;
    FILE* file_ = nullptr;
    long file_data_start_ = 0;
    uint8_t row_buf_[256]{};
    const uint8_t* mem_ = nullptr;

    bool valid() const {
      return (file_ || mem_) && w > 0 && h > 0;
    }

    const uint8_t* get_row(uint16_t y) {
      if (mem_)
        return mem_ + static_cast<size_t>(y) * src_stride;
      std::fseek(file_, file_data_start_ + static_cast<long>(static_cast<size_t>(y) * src_stride), SEEK_SET);
      size_t n = std::fread(row_buf_, 1, std::min(src_stride, static_cast<size_t>(sizeof(row_buf_))), file_);
      if (n < src_stride)
        std::fseek(file_, static_cast<long>(src_stride - n), SEEK_CUR);
      return row_buf_;
    }

    static Mgr2Source_ from_file(FILE* f) {
      Mgr2Source_ s;
      char magic[4];
      if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "MGR2", 4) != 0)
        return s;
      if (std::fread(&s.w, 2, 1, f) != 1 || std::fread(&s.h, 2, 1, f) != 1)
        return s;
      s.src_stride = (static_cast<size_t>(s.w) + 3) / 4;
      s.file_ = f;
      s.file_data_start_ = std::ftell(f);
      return s;
    }

    static Mgr2Source_ from_memory(const uint8_t* data, size_t size) {
      Mgr2Source_ s;
      if (size < 8 || std::memcmp(data, "MGR2", 4) != 0)
        return s;
      s.w = data[4] | (data[5] << 8);
      s.h = data[6] | (data[7] << 8);
      s.src_stride = (static_cast<size_t>(s.w) + 3) / 4;
      if (size < 8 + s.src_stride * static_cast<size_t>(s.h))
        return s;
      s.mem_ = data + 8;
      return s;
    }
  };

  void show_mgr2_sleep_(Mgr2Source_& src, bool deep_sleep_after) {
    // Decode one bit per pixel into the inactive buffer; the bit is set where
    // pred(state) holds (MGR2 states: 0=white, 1=light gray, 2=dark, 3=black).
    auto decode = [&](auto pred) {
      fill(false);
      for (uint16_t y = 0; y < src.h && y < DisplayFrame::kPhysicalHeight; ++y) {
        const uint8_t* src_row = src.get_row(y);
        uint8_t* dst = inactive_() + static_cast<size_t>(y) * DisplayFrame::kStride;
        for (int x = 0; x < static_cast<int>(src.w); x++) {
          int state = (src_row[x / 4] >> (6 - (x % 4) * 2)) & 0x3;
          if (pred(state))
            dst[x / 8] |= static_cast<uint8_t>(0x80 >> (x % 8));
        }
      }
    };

    if (display_.uc_grayscale()) {
      // UC-style (X3): show the black & white frame first, then one tint pass
      // pulls chosen pixels up from black to dark or light gray.
      decode([](int s) { return s == 0; });  // white only; grays start out black
      display_.full_refresh(inactive_(), RefreshMode::Half, false);
      decode([](int s) { return s == 2; });  // dark gray mask
      display_.write_ram_bw(inactive_());
      decode([](int s) { return s == 1 || s == 2; });  // any-gray mask
      display_.write_ram_red(inactive_());
      display_.grayscale_refresh_1pass(/*turnOffScreen=*/true);
    } else {
      // SSD1677 (X4): the two MGR2 bit-planes go to BW/RED RAM and the
      // factory-quality waveform renders all four levels in one pass.
      decode([](int s) { return (s & 1) != 0; });
      display_.write_ram_bw(inactive_());
      decode([](int s) { return (s >> 1) != 0; });
      display_.write_ram_red(inactive_());
      display_.grayscale_refresh_1pass(/*turnOffScreen=*/true);
    }
    if (deep_sleep_after)
      display_.deep_sleep();
  }

  // Render target for the full inactive buffer.
  // buf_x0 = -kPanelOffsetX so that app-space X=0 maps to buffer column kPanelOffsetX.
  RenderTarget full_target_() {
    return {inactive_(),
            DisplayFrame::kStride,
            0,
            0,
            DisplayFrame::kPhysicalWidth,
            DisplayFrame::kPhysicalHeight,
            -DisplayFrame::kPanelOffsetX};
  }

  // Render target for the mini loading-box buffer (absolute physical coords of the box).
  // buf_x0 = phys_x0 because the mini buffer is self-contained (no panel offset needed).
  static RenderTarget mini_target_(uint8_t* buf) {
    return {buf, kLoadStride, kLoadPhysX, kLoadPhysY, kLoadPhysW, kLoadPhysH, kLoadPhysX};
  }

  // Fill a physical horizontal span [x1, x2) on physical row `row` (absolute physical coords).
  // phys_x0 must be byte-aligned so that local_x has the same bit position as absolute x.
  static void fill_row_physical_(const RenderTarget& t, int row, int x1, int x2, bool white) {
    x1 = std::max(x1, t.phys_x0);
    x2 = std::min(x2, t.phys_x0 + t.phys_w);
    if (x1 >= x2 || row < t.phys_y0 || row >= t.phys_y0 + t.phys_h)
      return;
    const int lrow = row - t.phys_y0;
    const int lx1 = x1 - t.buf_x0;
    const int lx2 = x2 - t.buf_x0;
    const int bx1 = lx1 / 8;
    const int bx2 = (lx2 - 1) / 8;
    const auto lmask = static_cast<uint8_t>(0xFF >> (lx1 & 7));
    const auto rmask = static_cast<uint8_t>(0xFF << (7 - ((lx2 - 1) & 7)));
    uint8_t* rp = t.buf + lrow * t.stride;
    if (bx1 == bx2) {
      const auto m = static_cast<uint8_t>(lmask & rmask);
      if (white)
        rp[bx1] |= m;
      else
        rp[bx1] &= static_cast<uint8_t>(~m);
    } else {
      if (white)
        rp[bx1] |= lmask;
      else
        rp[bx1] &= static_cast<uint8_t>(~lmask);
      if (bx2 > bx1 + 1)
        memset(rp + bx1 + 1, white ? 0xFF : 0x00, bx2 - bx1 - 1);
      if (white)
        rp[bx2] |= rmask;
      else
        rp[bx2] &= static_cast<uint8_t>(~rmask);
    }
  }

  // Fill a physical rectangle (absolute physical coords).
  static void fill_rect_physical_(const RenderTarget& t, int rx, int ry, int rw, int rh, bool white) {
    const int x1 = std::max(rx, t.phys_x0);
    const int y1 = std::max(ry, t.phys_y0);
    const int x2 = std::min(rx + rw, t.phys_x0 + t.phys_w);
    const int y2 = std::min(ry + rh, t.phys_y0 + t.phys_h);
    if (x1 >= x2 || y1 >= y2)
      return;
    for (int row = y1; row < y2; ++row)
      fill_row_physical_(t, row, x1, x2, white);
  }

  // Fill physical column `pcol` for rows [py1, py2) (absolute physical coords).
  static void fill_col_physical_(const RenderTarget& t, int pcol, int py1, int py2, bool white) {
    py1 = std::max(py1, t.phys_y0);
    py2 = std::min(py2, t.phys_y0 + t.phys_h);
    if (pcol < t.phys_x0 || pcol >= t.phys_x0 + t.phys_w || py1 >= py2)
      return;
    const int lrow0 = py1 - t.phys_y0;
    const int lrow1 = py2 - t.phys_y0;
    const int lpcol = pcol - t.buf_x0;
    const int bidx = lpcol / 8;
    const uint8_t bit = static_cast<uint8_t>(0x80u >> (lpcol & 7));
    for (int r = lrow0; r < lrow1; ++r) {
      uint8_t* p = t.buf + r * t.stride + bidx;
      if (white)
        *p |= bit;
      else
        *p &= static_cast<uint8_t>(~bit);
    }
  }

  // Shared UTF-8 render core: draws text into any target from any GrayPlane.
  static int draw_text_impl_(const RenderTarget& t, int x, int baseline_y, const char* text, size_t len,
                             const BitmapFont& font, GrayPlane plane, bool white, FontStyle style,
                             Rotation rotation = Rotation::Deg90);

  // Render the loading box into a mini-buffer via the unified helpers.
  // Never reads or writes bufs_.
  static void render_loading_box_(uint8_t* mini, const char* text, int progress_pct) {
    const RenderTarget t = mini_target_(mini);
    memset(mini, 0xFF, kLoadBufBytes);  // white fill

    // Helper: fill a logical rect in black.
    // Deg90: fill_rect_physical_(t, phys_x=ly, phys_y=PhysH-lx-lw, phys_w=lh, phys_h=lw)
    auto fill = [&](int lx, int ly, int lw, int lh) {
      fill_rect_physical_(t, ly, DisplayFrame::kPhysicalHeight - lx - lw, lh, lw, /*white=*/false);
    };

    // Text centred horizontally, near top of loading region.
    const BitmapFont& font = ui_font_();
    if (text && *text) {
      const int tw = static_cast<int>(font.word_width(text, strlen(text), FontStyle::Regular));
      const int text_lx = kWidth / 2 - tw / 2;
      const int baseline_ly = kLoadLogY + 3 + static_cast<int>(font.baseline());
      draw_text_impl_(t, text_lx, baseline_ly, text, strlen(text), font, GrayPlane::BW, false, FontStyle::Regular);
    }

    // Outline: 160x7, rounded corners (corner pixels stay white).
    fill(kBarX + 1, kBarY, kBarW - 2, 1);              // top edge
    fill(kBarX + 1, kBarY + kBarH - 1, kBarW - 2, 1);  // bottom edge
    fill(kBarX, kBarY + 1, 1, kBarH - 2);              // left edge
    fill(kBarX + kBarW - 1, kBarY + 1, 1, kBarH - 2);  // right edge

    // Inner bar: 3 rows (kBarH=7 -> border(0), pad(1), bar(2,3,4), pad(5), border(6)).
    // Sloped right side: bottom row widest, each row above is 1px shorter.
    const int max_fill = kBarW - 4;  // usable width inside border
    const int filled = (progress_pct * max_fill) / 100;
    const int max_w = kBarW - 4;
    if (filled > 0) {
      fill(kBarX + 2, kBarY + 4, std::min(filled + 2, max_w), 1);  // bottom row - widest
      fill(kBarX + 2, kBarY + 3, std::min(filled + 1, max_w), 1);  // middle row
      fill(kBarX + 2, kBarY + 2, std::min(filled, max_w), 1);      // top row - narrowest
    }
  }
};

}  // namespace microreader

namespace microreader {

// Shared UTF-8 text rendering core. Renders into any RenderTarget from any GrayPlane.
// BW plane: ink pixels (bit=0) are drawn. Gray planes (LSB/MSB): set pixels (bit=1) are drawn.
inline int DrawBuffer::draw_text_impl_(const RenderTarget& t, int x, int baseline_y, const char* text, size_t len,
                                       const BitmapFont& font, GrayPlane plane, bool white, FontStyle style,
                                       Rotation rotation) {
  if (!text || len == 0 || !font.valid())
    return x;
  const char* p = text;
  const char* end = text + len;
  int cursor_q = x * 4;  // quarter pixels
  char32_t prev_cp = 0;

  while (p < end) {
    // Decode UTF-8
    char32_t cp = 0;
    uint8_t b = static_cast<uint8_t>(*p);
    if (b < 0x80) {
      cp = b;
      ++p;
    } else if (b < 0xE0 && p + 1 < end) {
      cp = (static_cast<char32_t>(b & 0x1F) << 6) | (static_cast<uint8_t>(p[1]) & 0x3F);
      p += 2;
    } else if (b < 0xF0 && p + 2 < end) {
      cp = (static_cast<char32_t>(b & 0x0F) << 12) | (static_cast<char32_t>(static_cast<uint8_t>(p[1]) & 0x3F) << 6) |
           (static_cast<uint8_t>(p[2]) & 0x3F);
      p += 3;
    } else if (b < 0xF8 && p + 3 < end) {
      cp = (static_cast<char32_t>(b & 0x07) << 18) | (static_cast<char32_t>(static_cast<uint8_t>(p[1]) & 0x3F) << 12) |
           (static_cast<char32_t>(static_cast<uint8_t>(p[2]) & 0x3F) << 6) | (static_cast<uint8_t>(p[3]) & 0x3F);
      p += 4;
    } else {
      ++p;
      cp = 0xFFFD;
    }

    if (prev_cp) {
      cursor_q += font.get_kerning_q(prev_cp, cp, style);
    }

    GlyphData g = font.glyph_data(cp, style);
    const uint8_t* bits = nullptr;
    bool invert = false;
    switch (plane) {
      case GrayPlane::BW:
        bits = g.bits;
        invert = false;
        break;
      case GrayPlane::LSB:
        bits = g.gray_lsb_bits;
        invert = true;
        break;
      case GrayPlane::MSB:
        bits = g.gray_msb_bits;
        invert = true;
        break;
    }
    if (bits) {
      draw_glyph_impl_(t, (cursor_q + 2) / 4, baseline_y, bits, g.bitmap_width, g.bitmap_height, g.x_offset, g.y_offset,
                       white, invert, rotation);
    }
    cursor_q += g.advance_width;
    cursor_q = ((cursor_q + 2) / 4) * 4;  // snap to full pixel - prevents fractional accumulation across characters
    prev_cp = cp;
  }
  return cursor_q / 4;
}

inline int DrawBuffer::draw_text_proportional(int x, int baseline_y, const char* text, size_t len,
                                              const BitmapFont& font, bool white, FontStyle style) {
  return draw_text_impl_(full_target_(), x, baseline_y, text, len, font, GrayPlane::BW, white, style, rotation_);
}

inline int DrawBuffer::draw_text_plane(uint8_t* buf, int x, int baseline_y, const char* text, size_t len,
                                       const BitmapFontSet& fonts, GrayPlane plane, bool white, FontStyle style,
                                       uint8_t size_pct) {
  const BitmapFont* f = fonts.get(size_pct);
  if (!f || !f->valid())
    return x;
  const RenderTarget t{buf,
                       DisplayFrame::kStride,
                       0,
                       0,
                       DisplayFrame::kPhysicalWidth,
                       DisplayFrame::kPhysicalHeight,
                       -DisplayFrame::kPanelOffsetX};
  return draw_text_impl_(t, x, baseline_y, text, len, *f, plane, white, style, rotation_);
}

inline void DrawBuffer::draw_layout_line(uint8_t* buf, int x_offset, int baseline_y, const PageLine& line,
                                         const BitmapFontSet& fonts, GrayPlane plane, bool white) {
  const RenderTarget t{buf,
                       DisplayFrame::kStride,
                       0,
                       0,
                       DisplayFrame::kPhysicalWidth,
                       DisplayFrame::kPhysicalHeight,
                       -DisplayFrame::kPanelOffsetX};

  // State for the current underline span (BW plane only).
  int ul_x0 = 0, ul_x1 = 0, ul_y = 0, ul_h = 0;
  const char* ul_href = nullptr;

  auto flush_ul = [&]() {
    if (!ul_href || ul_x1 <= ul_x0)
      return;
    if (rotation_ == Rotation::Deg0)
      fill_rect_physical_(t, ul_x0, ul_y, ul_x1 - ul_x0, ul_h, white);
    else
      fill_rect_physical_(t, ul_y, DisplayFrame::kPhysicalHeight - ul_x0 - (ul_x1 - ul_x0), ul_h, ul_x1 - ul_x0, white);
    ul_href = nullptr;
  };

  for (const auto& w : line.words) {
    if (w.len == 0)
      continue;
    const BitmapFont* f = fonts.get(w.size_pct);
    if (!f || !f->valid())
      continue;
    int x = x_offset + w.x;
    int word_baseline = baseline_y;
    if (w.vertical_align == VerticalAlign::Super)
      word_baseline -= static_cast<int>(fonts.y_advance(w.size_pct)) * 20 / 100;
    else if (w.vertical_align == VerticalAlign::Sub)
      word_baseline += static_cast<int>(fonts.y_advance(w.size_pct)) * 20 / 100;
    int end_x = draw_text_impl_(t, x, word_baseline, w.text, w.len, *f, plane, white, w.style, rotation_);

    if (plane == GrayPlane::BW) {
      if (w.href) {
        if (w.href != ul_href) {
          flush_ul();
          ul_href = w.href;
          ul_x0 = x;
          ul_y = word_baseline + static_cast<int>(f->underline_pos());
          ul_h = static_cast<int>(f->underline_thickness());
        }
        ul_x1 = end_x;
      } else {
        flush_ul();
      }
    }
  }
  if (plane == GrayPlane::BW)
    flush_ul();
}

}  // namespace microreader
