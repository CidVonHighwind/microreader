// Display Library for SPI e-paper panels from Dalian Good Display and boards from Waveshare.
// Requires HW SPI and Adafruit_GFX. Caution: the e-paper panels require 3.3V supply AND data lines!
//
// Display Library based on Demo Example from Good Display: https://www.good-display.com/companyfile/32/
//
// Author: Jean-Marc Zingg
//
// Version: see library.properties
//
// Library: https://github.com/ZinggJM/GxEPD2

#ifndef _EINK_BW_DISPLAY_H_
#define _EINK_BW_DISPLAY_H_

// uncomment next line to use class GFX of library GFX_Root instead of Adafruit_GFX
// #include <GFX.h>

#ifndef ENABLE_EINK_GFX
// default is off
#define ENABLE_EINK_GFX 0
#endif

#if ENABLE_EINK_GFX
#include "EInk_GFX.h"
#define EINK_GFX_BASE_CLASS EInk_GFX
#elif defined(_GFX_H_)
#define EINK_GFX_BASE_CLASS GFX
#else
#include <Adafruit_GFX.h>
#define EINK_GFX_BASE_CLASS Adafruit_GFX
#endif

#include "EInk_Base.h"

// Only include the display driver we actually use
#include "EInk426_BW.h"

template <typename EInk_Type, const uint16_t page_height>
class EInk_BW_Display : public EINK_GFX_BASE_CLASS {
 public:
  EInk_Type epd2;
#if ENABLE_EINK_GFX
  EInk_BW_Display(EInk_Type epd2_instance)
      : EINK_GFX_BASE_CLASS(epd2, EInk_Type::WIDTH_VISIBLE, EInk_Type::HEIGHT),
        epd2(epd2_instance)
#else
  EInk_BW_Display(EInk_Type epd2_instance)
      : EINK_GFX_BASE_CLASS(EInk_Type::WIDTH_VISIBLE, EInk_Type::HEIGHT),
        epd2(epd2_instance)
#endif
  {
    _page_height = page_height;
    _pages = (HEIGHT / _page_height) + ((HEIGHT % _page_height) > 0);
    _reverse = (epd2_instance.panel == EInk::GDE0213B1);
    _mirror = false;
    _using_partial_mode = false;
    _current_page = 0;
    setFullWindow();
  }

  uint16_t pages() {
    return _pages;
  }

  uint16_t pageHeight() {
    return _page_height;
  }

  bool mirror(bool m) {
    _swap_(_mirror, m);
    return m;
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) {
    if ((x < 0) || (x >= width()) || (y < 0) || (y >= height()))
      return;
    if (_mirror)
      x = width() - x - 1;
    // check rotation, move pixel around if necessary
    switch (getRotation()) {
      case 1:
        _swap_(x, y);
        x = WIDTH - x - 1;
        break;
      case 2:
        x = WIDTH - x - 1;
        y = HEIGHT - y - 1;
        break;
      case 3:
        _swap_(x, y);
        y = HEIGHT - y - 1;
        break;
    }
    // transpose partial window to 0,0
    x -= _pw_x;
    if (!_reverse)
      y -= _pw_y;
    else
      y = HEIGHT - _pw_y - y - 1;
    // clip to (partial) window
    if ((x < 0) || (x >= int16_t(_pw_w)) || (y < 0) || (y >= int16_t(_pw_h)))
      return;
    // adjust for current page
    y -= _current_page * _page_height;
    // check if in current page
    if ((y < 0) || (y >= int16_t(_page_height)))
      return;
    uint16_t i = x / 8 + y * (_pw_w / 8);
    if (color)
      _buffer[i] = (_buffer[i] | (1 << (7 - x % 8)));
    else
      _buffer[i] = (_buffer[i] & (0xFF ^ (1 << (7 - x % 8))));
  }

  void init(uint32_t serial_diag_bitrate = 0)  // = 0 : disabled
  {
    epd2.init(serial_diag_bitrate);
    _using_partial_mode = false;
    _current_page = 0;
    setFullWindow();
  }

  // init method with additional parameters:
  // initial false for re-init after processor deep sleep wake up, if display power supply was kept
  // this can be used to avoid the repeated initial full refresh on displays with fast partial update
  // NOTE: garbage will result on fast partial update displays, if initial full update is omitted after power loss
  // reset_duration = 10 is default; a value of 2 may help with "clever" reset circuit of newer boards from Waveshare
  // pulldown_rst_mode true for alternate RST handling to avoid feeding 5V through RST pin
  void init(uint32_t serial_diag_bitrate, bool initial, uint16_t reset_duration = 10, bool pulldown_rst_mode = false) {
    epd2.init(serial_diag_bitrate, initial, reset_duration, pulldown_rst_mode);
    _using_partial_mode = false;
    _current_page = 0;
    setFullWindow();
  }

  // init method with additional parameters:
  // SPIClass& spi: either SPI or alternate HW SPI channel
  // SPISettings spi_settings: e.g. for higher SPI speed selection
  void init(uint32_t serial_diag_bitrate, bool initial, uint16_t reset_duration, bool pulldown_rst_mode, SPIClass& spi,
            SPISettings spi_settings) {
    epd2.selectSPI(spi, spi_settings);
    epd2.init(serial_diag_bitrate, initial, reset_duration, pulldown_rst_mode);
    _using_partial_mode = false;
    _current_page = 0;
    setFullWindow();
  }

  // release SPI and control pins
  void end() {
    epd2.end();
  }

  void fillScreen(uint16_t color)  // 0x0 black, >0x0 white, to buffer
  {
    uint8_t data = (color == GxEPD_BLACK) ? 0x00 : 0xFF;
    for (uint16_t x = 0; x < sizeof(_buffer); x++) {
      _buffer[x] = data;
    }
  }

  // display buffer content to screen, useful for full screen buffer
  void display(bool partial_update_mode = false) {
    if (partial_update_mode)
      epd2.writeImage(_buffer, 0, 0, EInk_Type::WIDTH, _page_height);
    else
      epd2.writeImageForFullRefresh(_buffer, 0, 0, EInk_Type::WIDTH, _page_height);
    epd2.refresh(partial_update_mode);
    if (epd2.hasFastPartialUpdate) {
      epd2.writeImageAgain(_buffer, 0, 0, EInk_Type::WIDTH, _page_height);
    }
    if (!partial_update_mode)
      epd2.powerOff();
  }

  // display part of buffer content to screen, useful for full screen buffer
  // displayWindow, use parameters according to actual rotation.
  // x and w should be multiple of 8, for rotation 0 or 2,
  // y and h should be multiple of 8, for rotation 1 or 3,
  // else window is increased as needed,
  // this is an addressing limitation of the e-paper controllers
  void displayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    x = gx_uint16_min(x, width());
    y = gx_uint16_min(y, height());
    w = gx_uint16_min(w, width() - x);
    h = gx_uint16_min(h, height() - y);
    _rotate(x, y, w, h);
    uint16_t y_part = _reverse ? HEIGHT - h - y : y;
    epd2.writeImagePart(_buffer, x, y_part, EInk_Type::WIDTH, _page_height, x, y_part, w, h);
    epd2.refresh(x, y_part, w, h);
    if (epd2.hasFastPartialUpdate) {
      epd2.writeImagePartAgain(_buffer, x, y_part, EInk_Type::WIDTH, _page_height, x, y_part, w, h);
    }
  }

  void setFullWindow() {
    _using_partial_mode = false;
    _pw_x = 0;
    _pw_y = 0;
    _pw_w = EInk_Type::WIDTH;
    _pw_h = HEIGHT;
  }

  // setPartialWindow, use parameters according to actual rotation.
  // x and w should be multiple of 8, for rotation 0 or 2,
  // y and h should be multiple of 8, for rotation 1 or 3,
  // else window is increased as needed,
  // this is an addressing limitation of the e-paper controllers
  void setPartialWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    _pw_x = gx_uint16_min(x, width());
    _pw_y = gx_uint16_min(y, height());
    _pw_w = gx_uint16_min(w, width() - _pw_x);
    _pw_h = gx_uint16_min(h, height() - _pw_y);
    _rotate(_pw_x, _pw_y, _pw_w, _pw_h);
    _using_partial_mode = true;
    // make _pw_x, _pw_w multiple of 8
    _pw_w += _pw_x % 8;
    if (_pw_w % 8 > 0)
      _pw_w += 8 - _pw_w % 8;
    _pw_x -= _pw_x % 8;
    if (_reverse)
      _pw_y = HEIGHT - _pw_h - _pw_y;
  }

  void firstPage() {
    fillScreen(GxEPD_WHITE);
    _current_page = 0;
    _second_phase = false;
  }

  bool nextPage() {
    if (1 == _pages) {
      if (_using_partial_mode) {
        epd2.writeImage(_buffer, _pw_x, _pw_y, _pw_w, _pw_h);
        epd2.refresh(_pw_x, _pw_y, _pw_w, _pw_h);
        if (epd2.hasFastPartialUpdate) {
          epd2.writeImageAgain(_buffer, _pw_x, _pw_y, _pw_w, _pw_h);
          // epd2.refresh(_pw_x, _pw_y, _pw_w, _pw_h); // not needed
        }
      } else  // full update
      {
        epd2.writeImageForFullRefresh(_buffer, 0, 0, EInk_Type::WIDTH, HEIGHT);
        epd2.refresh(false);
        if (epd2.hasFastPartialUpdate) {
          epd2.writeImageAgain(_buffer, 0, 0, EInk_Type::WIDTH, HEIGHT);
          // epd2.refresh(true); // not needed
        }
        epd2.powerOff();
      }
      return false;
    }
    uint16_t page_ys = _current_page * _page_height;
    if (_using_partial_mode) {
      // Serial.print("  nextPage("); Serial.print(_pw_x); Serial.print(", "); Serial.print(_pw_y); Serial.print(", ");
      // Serial.print(_pw_w); Serial.print(", "); Serial.print(_pw_h); Serial.print(") P");
      // Serial.println(_current_page);
      uint16_t page_ye = _current_page < int16_t(_pages - 1) ? page_ys + _page_height : HEIGHT;
      uint16_t dest_ys = _pw_y + page_ys;  // transposed
      uint16_t dest_ye = gx_uint16_min(_pw_y + _pw_h, _pw_y + page_ye);
      if (dest_ye > dest_ys) {
        // Serial.print("writeImage("); Serial.print(_pw_x); Serial.print(", "); Serial.print(dest_ys); Serial.print(",
        // "); Serial.print(_pw_w); Serial.print(", "); Serial.print(dest_ye - dest_ys); Serial.println(")");
        if (!_second_phase)
          epd2.writeImage(_buffer, _pw_x, dest_ys, _pw_w, dest_ye - dest_ys);
        else
          epd2.writeImageAgain(_buffer, _pw_x, dest_ys, _pw_w, dest_ye - dest_ys);
      } else {
        // Serial.print("writeImage("); Serial.print(_pw_x); Serial.print(", "); Serial.print(dest_ys); Serial.print(",
        // "); Serial.print(_pw_w); Serial.print(", "); Serial.print(dest_ye - dest_ys); Serial.print(") skipped ");
        // Serial.print(dest_ys); Serial.print(".."); Serial.println(dest_ye);
      }
      _current_page++;
      if (_current_page == int16_t(_pages)) {
        _current_page = 0;
        if (!_second_phase) {
          epd2.refresh(_pw_x, _pw_y, _pw_w, _pw_h);
          if (epd2.hasFastPartialUpdate) {
            _second_phase = true;
            fillScreen(GxEPD_WHITE);
            return true;
          }
        }
        return false;
      }
      fillScreen(GxEPD_WHITE);
      return true;
    } else  // full update
    {
      if (!_second_phase)
        epd2.writeImageForFullRefresh(_buffer, 0, page_ys, EInk_Type::WIDTH,
                                      gx_uint16_min(_page_height, HEIGHT - page_ys));
      else
        epd2.writeImageAgain(_buffer, 0, page_ys, EInk_Type::WIDTH, gx_uint16_min(_page_height, HEIGHT - page_ys));
      _current_page++;
      if (_current_page == int16_t(_pages)) {
        _current_page = 0;
        if (epd2.hasFastPartialUpdate) {
          if (!_second_phase) {
            epd2.refresh(false);  // full update after first phase
            _second_phase = true;
            fillScreen(GxEPD_WHITE);
            return true;
          }
          // else epd2.refresh(true); // partial update after second phase
        } else
          epd2.refresh(false);  // full update after only phase
        epd2.powerOff();
        return false;
      }
      fillScreen(GxEPD_WHITE);
      return true;
    }
  }

  // GxEPD style paged drawing; drawCallback() is called as many times as needed
  void drawPaged(void (*drawCallback)(const void*), const void* pv) {
    if (1 == _pages) {
      fillScreen(GxEPD_WHITE);
      drawCallback(pv);
      if (_using_partial_mode) {
        epd2.writeImage(_buffer, _pw_x, _pw_y, _pw_w, _pw_h);
        epd2.refresh(_pw_x, _pw_y, _pw_w, _pw_h);
        if (epd2.hasFastPartialUpdate) {
          epd2.writeImageAgain(_buffer, _pw_x, _pw_y, _pw_w, _pw_h);
          // epd2.refresh(_pw_x, _pw_y, _pw_w, _pw_h); // not needed
        }
      } else  // full update
      {
        epd2.writeImageForFullRefresh(_buffer, 0, 0, EInk_Type::WIDTH, HEIGHT);
        epd2.refresh(false);
        if (epd2.hasFastPartialUpdate) {
          epd2.writeImageAgain(_buffer, 0, 0, EInk_Type::WIDTH, HEIGHT);
          // epd2.refresh(true); // not needed
          epd2.powerOff();
        }
      }
      return;
    }
    if (_using_partial_mode) {
      for (uint16_t phase = 1; phase <= 2; phase++) {
        for (_current_page = 0; _current_page < _pages; _current_page++) {
          uint16_t page_ys = _current_page * _page_height;
          uint16_t page_ye = _current_page < (_pages - 1) ? page_ys + _page_height : HEIGHT;
          uint16_t dest_ys = _pw_y + page_ys;  // transposed
          uint16_t dest_ye = gx_uint16_min(_pw_y + _pw_h, _pw_y + page_ye);
          if (dest_ye > dest_ys) {
            fillScreen(GxEPD_WHITE);
            drawCallback(pv);
            if (phase == 1)
              epd2.writeImage(_buffer, _pw_x, dest_ys, _pw_w, dest_ye - dest_ys);
            else
              epd2.writeImageAgain(_buffer, _pw_x, dest_ys, _pw_w, dest_ye - dest_ys);
          }
        }
        epd2.refresh(_pw_x, _pw_y, _pw_w, _pw_h);
        if (!epd2.hasFastPartialUpdate)
          break;
        // else make both controller buffers have equal content
      }
    } else  // full update
    {
      for (_current_page = 0; _current_page < _pages; _current_page++) {
        uint16_t page_ys = _current_page * _page_height;
        fillScreen(GxEPD_WHITE);
        drawCallback(pv);
        epd2.writeImageForFullRefresh(_buffer, 0, page_ys, EInk_Type::WIDTH,
                                      gx_uint16_min(_page_height, HEIGHT - page_ys));
      }
      epd2.refresh(false);  // full update after first phase
      if (epd2.hasFastPartialUpdate) {
        // make both controller buffers have equal content
        for (_current_page = 0; _current_page < _pages; _current_page++) {
          uint16_t page_ys = _current_page * _page_height;
          fillScreen(GxEPD_WHITE);
          drawCallback(pv);
          epd2.writeImageAgain(_buffer, 0, page_ys, EInk_Type::WIDTH, gx_uint16_min(_page_height, HEIGHT - page_ys));
        }
        // epd2.refresh(true); // partial update after second phase // not needed
      }
      epd2.powerOff();
    }
    _current_page = 0;
  }

  void drawInvertedBitmap(int16_t x, int16_t y, const uint8_t bitmap[], int16_t w, int16_t h, uint16_t color) {
    // taken from Adafruit_GFX.cpp, modified
    int16_t byteWidth = (w + 7) / 8;  // Bitmap scanline pad = whole byte
    uint8_t byte = 0;
    for (int16_t j = 0; j < h; j++) {
      for (int16_t i = 0; i < w; i++) {
        if (i & 7)
          byte <<= 1;
        else {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
          byte = pgm_read_byte(&bitmap[j * byteWidth + i / 8]);
#else
          byte = bitmap[j * byteWidth + i / 8];
#endif
        }
        if (!(byte & 0x80)) {
          drawPixel(x + i, y + j, color);
        }
      }
    }
  }

  //  Support for Bitmaps (Sprites) to Controller Buffer and to Screen
  void clearScreen(uint8_t value = 0xFF)  // init controller memory and screen (default white)
  {
    epd2.clearScreen(value);
  }
  void writeScreenBuffer(uint8_t value = 0xFF)  // init controller memory (default white)
  {
    epd2.writeScreenBuffer(value);
  }
  // write to controller memory, without screen refresh; x and w should be multiple of 8
  void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false,
                  bool mirror_y = false, bool pgm = false) {
    epd2.writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
  }
  void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                      int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false,
                      bool pgm = false) {
    epd2.writeImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  }
  void writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert,
                  bool mirror_y, bool pgm) {
    epd2.writeImage(black, color, x, y, w, h, invert, mirror_y, pgm);
  }
  void writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h) {
    epd2.writeImage(black, color, x, y, w, h, false, false, false);
  }
  void writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap,
                      int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y,
                      bool pgm) {
    epd2.writeImagePart(black, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  }
  void writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap,
                      int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h) {
    epd2.writeImagePart(black, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, false, false, false);
  }
  // write sprite of native data to controller memory, without screen refresh; x and w should be multiple of 8
  void writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert,
                   bool mirror_y, bool pgm) {
    epd2.writeNative(data1, data2, x, y, w, h, invert, mirror_y, pgm);
  }
  // write to controller memory, with screen refresh; x and w should be multiple of 8
  void drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false,
                 bool mirror_y = false, bool pgm = false) {
    epd2.drawImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
  }
  void drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                     int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false,
                     bool pgm = false) {
    epd2.drawImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  }
  void drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert,
                 bool mirror_y, bool pgm) {
    epd2.drawImage(black, color, x, y, w, h, invert, mirror_y, pgm);
  }
  void drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h) {
    epd2.drawImage(black, color, x, y, w, h, false, false, false);
  }
  void drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap,
                     int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y,
                     bool pgm) {
    epd2.drawImagePart(black, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  }
  void drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap,
                     int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h) {
    epd2.drawImagePart(black, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, false, false, false);
  }
  // write sprite of native data to controller memory, with screen refresh; x and w should be multiple of 8
  void drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert,
                  bool mirror_y, bool pgm) {
    epd2.drawNative(data1, data2, x, y, w, h, invert, mirror_y, pgm);
  }
  void refresh(bool partial_update_mode = false)  // screen refresh from controller memory to full screen
  {
    epd2.refresh(partial_update_mode);
    if (!partial_update_mode)
      epd2.powerOff();
  }
  void refresh(int16_t x, int16_t y, int16_t w, int16_t h)  // screen refresh from controller memory, partial screen
  {
    epd2.refresh(x, y, w, h);
  }
  // turns off generation of panel driving voltages, avoids screen fading over time
  void powerOff() {
    epd2.powerOff();
  }
  // turns powerOff() and sets controller to deep sleep for minimum power use, ONLY if wakeable by RST (rst >= 0)
  void hibernate() {
    epd2.hibernate();
  }

 private:
  template <typename T>
  static inline void _swap_(T& a, T& b) {
    T t = a;
    a = b;
    b = t;
  };
  static inline uint16_t gx_uint16_min(uint16_t a, uint16_t b) {
    return (a < b ? a : b);
  };
  static inline uint16_t gx_uint16_max(uint16_t a, uint16_t b) {
    return (a > b ? a : b);
  };
  void _rotate(uint16_t& x, uint16_t& y, uint16_t& w, uint16_t& h) {
    switch (getRotation()) {
      case 1:
        _swap_(x, y);
        _swap_(w, h);
        x = WIDTH - x - w;
        break;
      case 2:
        x = WIDTH - x - w;
        y = HEIGHT - y - h;
        break;
      case 3:
        _swap_(x, y);
        _swap_(w, h);
        y = HEIGHT - y - h;
        break;
    }
  }

 private:
  uint8_t _buffer[(EInk_Type::WIDTH / 8) * page_height];
  bool _using_partial_mode, _second_phase, _mirror, _reverse;
  uint16_t _width_bytes, _pixel_bytes;
  int16_t _current_page;
  uint16_t _pages, _page_height;
  uint16_t _pw_x, _pw_y, _pw_w, _pw_h;
};

#endif
