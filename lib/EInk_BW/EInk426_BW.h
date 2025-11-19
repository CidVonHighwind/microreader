// GDEQ0426T82 4.26" e-ink display driver
// Controller: SSD1677

#ifndef _EINK426_BW_H_
#define _EINK426_BW_H_

#include "EInk_Base.h"

class EInk426_BW : public EInk_Base {
 public:
  // SSD1677 Command Set
  static const uint8_t CMD_DRIVER_OUTPUT_CONTROL = 0x01;
  static const uint8_t CMD_SOFT_RESET = 0x12;
  static const uint8_t CMD_DEEP_SLEEP = 0x10;
  static const uint8_t CMD_DATA_ENTRY_MODE = 0x11;
  static const uint8_t CMD_MASTER_ACTIVATION = 0x20;
  static const uint8_t CMD_DISPLAY_UPDATE_CTRL1 = 0x21;
  static const uint8_t CMD_DISPLAY_UPDATE_CTRL2 = 0x22;
  static const uint8_t CMD_WRITE_RAM_BW = 0x24;
  static const uint8_t CMD_WRITE_RAM_RED = 0x26;
  static const uint8_t CMD_WRITE_VCOM = 0x2C;
  static const uint8_t CMD_WRITE_LUT = 0x32;
  static const uint8_t CMD_BORDER_WAVEFORM = 0x3C;
  static const uint8_t CMD_SET_RAM_X_RANGE = 0x44;
  static const uint8_t CMD_SET_RAM_Y_RANGE = 0x45;
  static const uint8_t CMD_SET_RAM_X_COUNTER = 0x4E;
  static const uint8_t CMD_SET_RAM_Y_COUNTER = 0x4F;
  static const uint8_t CMD_BOOSTER_SOFT_START = 0x0C;
  static const uint8_t CMD_TEMP_SENSOR_CONTROL = 0x18;

  // Data Entry Mode values
  static const uint8_t DATA_ENTRY_X_INC_Y_DEC = 0x01;  // X increment, Y decrement (Y reversed)
  static const uint8_t DATA_ENTRY_X_INC_Y_INC = 0x03;  // X increment, Y increment

  // Display Update Control values
  static const uint8_t UPDATE_MODE_FULL_REFRESH = 0xF7;    // Full display refresh
  static const uint8_t UPDATE_MODE_FULL_FAST = 0xD7;       // Fast full refresh (reduced temp range)
  static const uint8_t UPDATE_MODE_PARTIAL_FAST = 0xC7;    // Partial with fast custom LUT
  static const uint8_t UPDATE_MODE_PARTIAL_NORMAL = 0xFC;  // Partial with default LUT
  static const uint8_t UPDATE_CTRL1_BYPASS_RED = 0x40;     // Display Update Control 1: bypass RED
  static const uint8_t UPDATE_CTRL1_NORMAL = 0x00;         // Display Update Control 1: normal
  static const uint8_t UPDATE_POWER_ON = 0xE0;             // Power on sequence
  static const uint8_t UPDATE_POWER_OFF = 0x83;            // Power off sequence

  // Temperature Sensor values
  static const uint8_t TEMP_SENSOR_INTERNAL = 0x80;  // Use internal temperature sensor
  static const uint8_t TEMP_SENSOR_EXTERNAL = 0x48;  // Use external temperature sensor

  // Voltage Control Commands
  static const uint8_t CMD_GATE_VOLTAGE = 0x03;    // VGH setting
  static const uint8_t CMD_SOURCE_VOLTAGE = 0x04;  // VSH1, VSH2, VSL setting

  // attributes
  static const uint16_t WIDTH = 800;  // source, max 960
  static const uint16_t WIDTH_VISIBLE = WIDTH;
  static const uint16_t HEIGHT = 480;  // gates, max 680
  static const EInk::Panel panel = EInk::GDEQ0426T82;
  static const bool hasColor = false;
  static const bool hasPartialUpdate = true;
  static const bool hasFastPartialUpdate = true;
  static const bool useFastFullUpdate = true;        // set false for extended (low) temperature range
  static const uint16_t power_on_time = 100;         // ms, e.g. 83873us
  static const uint16_t power_off_time = 200;        // ms, e.g. 138810us
  static const uint16_t full_refresh_time = 1600;    // ms, e.g. 1567341us
  static const uint16_t partial_refresh_time = 600;  // ms, e.g. 499962us
  // constructor
  EInk426_BW(int16_t cs, int16_t dc, int16_t rst, int16_t busy);
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
  void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                      int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false,
                      bool pgm = false);
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
  void drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                     int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false,
                     bool pgm = false);
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
  void setCustomLUT(bool enabled);  // Enable or disable custom LUT (loads LUT when enabled, resets when disabled)
  void resetDisplay();              // Reset display to default settings (call after custom LUT testing)
  uint16_t calculateLUTRefreshTime(const uint8_t* lut);  // Calculate refresh time from LUT timing parameters
  uint16_t getCustomLUTRefreshTime() const {
    return _custom_lut_refresh_time;
  }  // Get calculated refresh time for active custom LUT

 private:
  static const unsigned char lut_custom_test[];
  bool _custom_lut_active = false;        // Track if custom LUT is loaded
  uint16_t _custom_lut_refresh_time = 0;  // Calculated refresh time for custom LUT
  void _writeScreenBuffer(uint8_t command, uint8_t value);
  void _writeImage(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                   bool invert = false, bool mirror_y = false, bool pgm = false);
  void _writeImagePart(uint8_t command, const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap,
                       int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false,
                       bool mirror_y = false, bool pgm = false);
  void _setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void _PowerOn();
  void _PowerOff();
  void _InitDisplay();
  void _Update_Full();
  void _Update_Part();
  void _LoadCustomLUT();  // Load custom LUT with voltage initialization
};

#endif
