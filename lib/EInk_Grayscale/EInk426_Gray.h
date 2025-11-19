// Display Library for SPI e-paper panels from Dalian Good Display and boards from Waveshare.
// Requires HW SPI and Adafruit_GFX. Caution: the e-paper panels require 3.3V supply AND data lines!
//
// based on Demo Example from Good Display, available here:
// https://www.good-display.com/comp/xcompanyFile/downloadNew.do?appId=24&fid=1722&id=1158 Panel: GDEQ0426T82 :
// https://www.good-display.com/product/457.html Controller : SSD1677 :
// https://v4.cecdn.yun300.cn/100001_1909185148/SSD1677.pdf code extracted for LUT and settings from
// https://github.com/waveshare/e-Paper
//
// Author: Jean-Marc Zingg
//
// Version: see library.properties
//
// Library: https://github.com/ZinggJM/GxEPD2

#ifndef _EINK426_GRAY_H_
#define _EINK426_GRAY_H_

#include "EInk_Gray_Base.h"

class EInk426_Gray : public EInk_Gray_Base {
 public:
  // attributes
  static const uint16_t WIDTH = 800;
  static const uint16_t HEIGHT = 480;
  static const EInk_Gray::Panel panel = EInk_Gray::GDEQ0426T82;
  static const bool hasColor = false;
  static const bool hasPartialUpdate = true;  // grey refresh uses full screen
  // static const bool hasFastPartialUpdate = false; // with grey refresh
  static const bool hasFastPartialUpdate = true;     // with b/w refresh
  static const bool useFastFullUpdate = true;        // set false for extended (low) temperature range
  static const uint16_t power_on_time = 100;         // ms, e.g. 81000us
  static const uint16_t power_off_time = 150;        // ms, e.g. 140350us
  static const uint16_t grey_refresh_time = 4000;    // ms, e.g. 3943998us
  static const uint16_t full_refresh_time = 1800;    // ms, e.g. 1706000us
  static const uint16_t partial_refresh_time = 510;  // ms, e.g. 501000us
  // constructor
  EInk426_Gray(int16_t cs, int16_t dc, int16_t rst, int16_t busy);
  // methods (virtual)
  //  Support for Bitmaps (Sprites) to Controller Buffer and to Screen
  void clearScreen(uint8_t value = 0xFF);             // init controller memory and screen (default white)
  void writeScreenBuffer(uint8_t value = 0xFF);       // init controller memory (default white)
  void writeScreenBufferAgain(uint8_t value = 0xFF);  // init previous buffer controller memory (default white)
  // write to controller memory, without screen refresh; x and w should be multiple of 8
  void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false,
                  bool mirror_y = false, bool pgm = false);
  void writeImageForFullRefresh(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false,
                                bool mirror_y = false, bool pgm = false);
  void writeImage_4G(const uint8_t bitmap[], uint8_t bpp, int16_t x, int16_t y, int16_t w, int16_t h,
                     bool invert = false, bool mirror_y = false, bool pgm = false);
  void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                      int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false,
                      bool pgm = false);
  void writeImagePart_4G(const uint8_t bitmap[], uint8_t bpp, int16_t x_part, int16_t y_part, int16_t w_bitmap,
                         int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false,
                         bool mirror_y = false, bool pgm = false);
  void writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h,
                  bool invert = false, bool mirror_y = false, bool pgm = false);
  void writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap,
                      int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false,
                      bool mirror_y = false, bool pgm = false);
  // for differential update: set current and previous buffers equal (for fast partial update to work correctly)
  void writeImageAgain(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false,
                       bool mirror_y = false, bool pgm = false);
  void writeImagePartAgain(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                           int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false,
                           bool pgm = false);
  // write sprite of native data to controller memory, without screen refresh; x and w should be multiple of 8
  void writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h,
                   bool invert = false, bool mirror_y = false, bool pgm = false);
  // write to controller memory, with screen refresh; x and w should be multiple of 8
  void drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false,
                 bool mirror_y = false, bool pgm = false);
  void drawImage_4G(const uint8_t bitmap[], uint8_t bpp, int16_t x, int16_t y, int16_t w, int16_t h,
                    bool invert = false, bool mirror_y = false, bool pgm = false);
  void drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                     int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false,
                     bool pgm = false);
  void drawImagePart_4G(const uint8_t bitmap[], uint8_t bpp, int16_t x_part, int16_t y_part, int16_t w_bitmap,
                        int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false,
                        bool mirror_y = false, bool pgm = false);
  void drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h,
                 bool invert = false, bool mirror_y = false, bool pgm = false);
  void drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap,
                     int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false,
                     bool mirror_y = false, bool pgm = false);
  // write sprite of native data to controller memory, with screen refresh; x and w should be multiple of 8
  void drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h,
                  bool invert = false, bool mirror_y = false, bool pgm = false);
  void refresh(bool partial_update_mode = false);            // screen refresh from controller memory to full screen
  void refresh(int16_t x, int16_t y, int16_t w, int16_t h);  // screen refresh from controller memory, partial screen
  void powerOff();   // turns off generation of panel driving voltages, avoids screen fading over time
  void hibernate();  // turns powerOff() and sets controller to deep sleep for minimum power use, ONLY if wakeable by
                     // RST (rst >= 0)
  void drawGreyLevels();
  void drawSolidGreyLevel(uint8_t level);  // draw solid gray level: 0=white, 1=light gray, 2=dark gray, 3=black

 private:
  void _writeScreenBuffer(uint8_t command, uint8_t value);
  void _writeScreenBufferArea(uint8_t command, uint8_t value, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void _writeImage(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                   bool invert = false, bool mirror_y = false, bool pgm = false);
  void _writeImagePart(uint8_t command, const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap,
                       int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false,
                       bool mirror_y = false, bool pgm = false);
  void _setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void _PowerOn();
  void _PowerOff();
  void _InitDisplay();
  void _Force_Init_Full();
  void _Init_Full();
  void _Init_4G();
  void _Init_Part();
  void _Update_Full();
  void _Update_4G();
  void _Update_Part();

 private:
  enum { full_refresh, grey_refresh, fast_refresh, forced_full_refresh } _refresh_mode;
  static const unsigned char lut_4G[];
};

#endif
