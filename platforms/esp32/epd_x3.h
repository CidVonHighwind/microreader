#pragma once

// E-ink driver for the Xteink X3 (792x528 panel). Selected by MICROREADER_X3;
// the X4 uses epd.h (SSD1677) instead.
//
// X3 units ship with one of two display controllers that speak the same
// "UC-style" command set but need different init code and waveforms:
//   - UC8253 (the common one; the stock firmware's default)
//   - UC8279 (newer units)
// begin() tells them apart the way the stock firmware does: it reads the
// version register (0x70) before the SPI bus is set up and looks at byte 2
// (0x66 = UC8279, 0xFF = UC8253). When the read is inconclusive it assumes
// UC8253.
//
// The refresh flows, waveform tables (LUTs) and the probe are ported from
// PapyriX (github.com/bigbag/papyrix-reader) and the FreeInk SDK, both MIT
// licensed; see THIRD_PARTY.md. Unlike the X4's SSD1677, these controllers
// take their waveforms from us on every refresh, scan rows bottom-to-top
// (planes are sent with mirrored rows), and signal BUSY with a LOW level.
//
// Grayscale works differently from the X4 too: the panel first shows the
// black & white version, then one more pass pulls chosen pixels up to dark
// or light gray. DrawBuffer picks that path when uc_grayscale() is true:
//   write_ram_bw()  = the "make it dark gray" mask  -> RAM 0x10
//   write_ram_red() = the "make it any gray" mask   -> RAM 0x13
//   grayscale_refresh_1pass() = the tint pass
//
// Not available on the X3 (quiet no-ops): the serial-upload custom LUTs
// (SSD1677 format), the partial-region "Converting..." box, and the
// anti-aliased reader font (ReaderScreen skips it when uc_grayscale()).

#include <cstring>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "microreader/display/DrawBuffer.h"

#define EPD_SCLK GPIO_NUM_8
#define EPD_MOSI GPIO_NUM_10
#define EPD_CS GPIO_NUM_21
#define EPD_DC GPIO_NUM_4
#define EPD_RST GPIO_NUM_5
#define EPD_BUSY GPIO_NUM_6

// clang-format off
namespace x3lut {

// UC8253 waveforms (42 bytes per table; 0x20=VCOM 0x21=WW 0x22=BW 0x23=WB 0x24=BB).
// From PapyriX Display.cpp (MIT), originally reverse-engineered from the stock
// X3 firmware / FreeInk SDK.

// Quality refresh (used as the conditioning pass after a full sync).
static const uint8_t kVcomFull[42] = {0x00, 0x06, 0x02, 0x06, 0x06, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00, 0x01};
static const uint8_t kWwFull[42]   = {0x20, 0x06, 0x02, 0x06, 0x06, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00, 0x01};
static const uint8_t kBwFull[42]   = {0xAA, 0x06, 0x02, 0x06, 0x06, 0x01, 0x80, 0x05, 0x01, 0x00, 0x00, 0x01};
static const uint8_t kWbFull[42]   = {0x55, 0x06, 0x02, 0x06, 0x06, 0x01, 0x40, 0x05, 0x01, 0x00, 0x00, 0x01};
static const uint8_t kBbFull[42]   = {0x10, 0x06, 0x02, 0x06, 0x06, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00, 0x01};

// Image write (full sync: both RAMs get inverted data first).
static const uint8_t kVcomImg[42] = {0x00, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x00, 0x0C, 0x02, 0x07, 0x02, 0x01,
                                     0x00, 0x01, 0x00, 0x02, 0x00, 0x01};
static const uint8_t kWwImg[42]   = {0xA8, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x44, 0x0C, 0x02, 0x07, 0x02, 0x01,
                                     0x04, 0x01, 0x00, 0x02, 0x00, 0x01};
static const uint8_t kBwImg[42]   = {0x80, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x62, 0x0C, 0x02, 0x07, 0x02, 0x01,
                                     0x00, 0x01, 0x00, 0x02, 0x00, 0x01};
static const uint8_t kWbImg[42]   = {0x88, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x60, 0x0C, 0x02, 0x07, 0x02, 0x01,
                                     0x00, 0x01, 0x00, 0x02, 0x00, 0x01};
static const uint8_t kBbImg[42]   = {0x00, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x4A, 0x0C, 0x02, 0x07, 0x02, 0x01,
                                     0x88, 0x01, 0x00, 0x02, 0x00, 0x01};

// Fast differential page turns.
static const uint8_t kVcomTurbo[42] = {0x00, 0x04, 0x02, 0x04, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00, 0x00, 0x01};
static const uint8_t kWwTurbo[42]   = {0x20, 0x04, 0x02, 0x04, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00, 0x00, 0x01};
static const uint8_t kBwTurbo[42]   = {0xAA, 0x04, 0x02, 0x04, 0x04, 0x01, 0x80, 0x04, 0x01, 0x00, 0x00, 0x01};
static const uint8_t kWbTurbo[42]   = {0x55, 0x04, 0x02, 0x04, 0x04, 0x01, 0x40, 0x04, 0x01, 0x00, 0x00, 0x01};
static const uint8_t kBbTurbo[42]   = {0x10, 0x04, 0x02, 0x04, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00, 0x00, 0x01};

// Scrub refresh (state-independent; used for "half" refreshes).
static const uint8_t kVcomHalf[42] = {0x00, 0x06, 0x01, 0x06, 0x06, 0x01, 0x00, 0x04, 0x01, 0x01, 0x00, 0x01};
static const uint8_t kWwHalf[42]   = {0xAA, 0x06, 0x01, 0x06, 0x06, 0x01, 0xA0, 0x04, 0x01, 0x01, 0x00, 0x01};
static const uint8_t kBwHalf[42]   = {0xAA, 0x06, 0x01, 0x06, 0x06, 0x01, 0xA0, 0x04, 0x01, 0x01, 0x00, 0x01};
static const uint8_t kWbHalf[42]   = {0x55, 0x06, 0x01, 0x06, 0x06, 0x01, 0x50, 0x04, 0x01, 0x01, 0x00, 0x01};
static const uint8_t kBbHalf[42]   = {0x55, 0x06, 0x01, 0x06, 0x06, 0x01, 0x50, 0x04, 0x01, 0x01, 0x00, 0x01};

// Grayscale tint pass (over an already-displayed black & white frame):
//   (0x10,0x13) = (1,1) -> dark gray, (0,1) -> light gray, else hold.
static const uint8_t kVcomGray[42] = {0x00, 0x03, 0x02, 0x01, 0x01, 0x01};
static const uint8_t kWwGray[42]   = {0x20, 0x03, 0x02, 0x01, 0x01, 0x01};
static const uint8_t kBwGray[42]   = {0x80, 0x03, 0x02, 0x01, 0x01, 0x01};
static const uint8_t kWbGray[42]   = {0x00, 0x03, 0x02, 0x01, 0x01, 0x01};
static const uint8_t kBbGray[42]   = {0x00, 0x03, 0x02, 0x01, 0x01, 0x01};

// UC8279 waveform banks (the controller pads each 42/48-byte table itself;
// trailing zeros matter, so full-size arrays).
static const uint8_t k79Gc[5][43] = {
    {0x20, 0x01, 0x1A, 0x1A, 0x01, 0x00, 0x01, 0x01}, {0x21, 0x01, 0x5A, 0x9A, 0x01, 0x00, 0x01, 0x01},
    {0x22, 0x01, 0x1A, 0x9A, 0x01, 0x00, 0x01, 0x01}, {0x23, 0x01, 0x1A, 0x5A, 0x01, 0x00, 0x01, 0x01},
    {0x24, 0x01, 0x9A, 0x5A, 0x01, 0x00, 0x01, 0x01},
};
static const uint8_t k79Du[5][43] = {
    {0x20, 0x01, 0x07, 0x01, 0x06, 0x06, 0x01, 0x01, 0x01, 0x06, 0x01, 0x00, 0x00, 0x01},
    {0x21, 0x01, 0x07, 0x81, 0x06, 0x06, 0x01, 0x01, 0x01, 0x06, 0x01, 0x00, 0x00, 0x01},
    {0x22, 0x01, 0x87, 0x81, 0x86, 0x86, 0x01, 0x01, 0x01, 0x86, 0x01, 0x00, 0x00, 0x01},
    {0x23, 0x01, 0x47, 0x41, 0x46, 0x46, 0x01, 0x01, 0x01, 0x46, 0x01, 0x00, 0x00, 0x01},
    {0x24, 0x01, 0x07, 0x01, 0x06, 0x06, 0x01, 0x01, 0x01, 0x06, 0x01, 0x00, 0x00, 0x01},
};
static const uint8_t k79Gray[5][49] = {
    {0x01, 0x03, 0x02, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01},
    {0x01, 0x03, 0x02, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01},
    {0x01, 0x83, 0x82, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01},
    {0x01, 0x03, 0x82, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01},
    {0x01, 0x03, 0x02, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01},
};
// UC8279 init script: command, length, data...
static const uint8_t k79Init[] = {
    0x00, 2, 0x3F, 0x4A,                                          // panel setting
    0x91, 0,                                                      // partial in (stays on)
    0x90, 9, 0x00, 0x00, 0x03, 0x17, 0x00, 0x00, 0x02, 0x0F, 0x01,  // full window
    0x03, 1, 0x20,                                                // gate voltage
    0x01, 5, 0x43, 0x00, 0x78, 0x78, 0x17,                        // power setting
    0x82, 1, 0x24,                                                // VCOM DC
    0x06, 3, 0x25, 0x25, 0x3C,                                    // booster
    0x30, 1, 0x0F,                                                // PLL
    0xE1, 1, 0x02,
};
static const uint8_t k79FullWindow[9] = {0x00, 0x00, 0x03, 0x17, 0x00, 0x00, 0x02, 0x0F, 0x01};
constexpr uint8_t k79CdiFirst = 0x97;
constexpr uint8_t k79CdiLater = 0xD7;

}  // namespace x3lut
// clang-format on

enum EpdRefreshMode { EPD_FULL_REFRESH, EPD_HALF_REFRESH, EPD_FAST_REFRESH };

class EInkDisplay : public microreader::IDisplay {
 public:
  static constexpr uint16_t DISPLAY_WIDTH = microreader::DisplayFrame::kPanelWidth;    // 792
  static constexpr uint16_t DISPLAY_HEIGHT = microreader::DisplayFrame::kPhysicalHeight;  // 528
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = microreader::DisplayFrame::kStride;  // 99
  static constexpr uint32_t BUFFER_SIZE = microreader::DisplayFrame::kPixelBytes;      // 52272

  enum class Controller : uint8_t { UC8253, UC8279 };

  bool isScreenOn = false;
  bool inDeepSleep_ = false;
  bool in_grayscale_mode_ = false;

  bool in_grayscale_mode() const override {
    return in_grayscale_mode_;
  }

  // UC-style controllers: BUSY is LOW while working.
  bool is_busy() const override {
    return gpio_get_level(EPD_BUSY) == 0;
  }

  bool uc_grayscale() const override {
    return true;
  }

  Controller controller() const {
    return controller_;
  }

  // The serial LUT upload sends SSD1677-format tables; nothing to do here.
  void set_grayscale_lut(const uint8_t*) {}
  void clear_grayscale_lut() {}
  void set_grayscale_revert_lut(const uint8_t*) {}
  void clear_grayscale_revert_lut() {}

  void begin() {
    // Probe the controller first: it bit-bangs the display pins, so it must
    // run before they are handed to the SPI peripheral.
    controller_ = probe_controller_();

    gpio_config_t out_cfg{};
    out_cfg.pin_bit_mask = (1ULL << EPD_CS) | (1ULL << EPD_DC) | (1ULL << EPD_RST);
    out_cfg.mode = GPIO_MODE_OUTPUT;
    gpio_config(&out_cfg);
    gpio_config_t in_cfg{};
    in_cfg.pin_bit_mask = (1ULL << EPD_BUSY);
    in_cfg.mode = GPIO_MODE_INPUT;
    gpio_config(&in_cfg);
    gpio_set_level(EPD_CS, 1);
    gpio_set_level(EPD_DC, 1);

    spi_bus_config_t bus{};
    bus.mosi_io_num = EPD_MOSI;
    bus.miso_io_num = GPIO_NUM_7;  // shared with the SD card
    bus.sclk_io_num = EPD_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 4096;
    spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t dev{};
    dev.clock_speed_hz = (controller_ == Controller::UC8279 ? 20 : 10) * 1000 * 1000;
    dev.mode = 0;
    dev.spics_io_num = -1;
    dev.queue_size = 1;
    spi_bus_add_device(SPI2_HOST, &dev, &spi_);

    resetDisplay();
    initDisplayController(true);
    ESP_LOGI(kTag, "X3 panel ready (%s)", controller_ == Controller::UC8279 ? "UC8279" : "UC8253");
  }

  // ---- microreader::IDisplay ----

  void full_refresh(const uint8_t* pixels, microreader::RefreshMode mode, bool turnOffScreen) override {
    in_grayscale_mode_ = false;
    wakeIfNeeded();
    if (controller_ == Controller::UC8279) {
      display79_(pixels, mode == microreader::RefreshMode::Half ? EPD_HALF_REFRESH : EPD_FULL_REFRESH, turnOffScreen);
      return;
    }
    display53_(pixels, mode == microreader::RefreshMode::Half ? EPD_HALF_REFRESH : EPD_FULL_REFRESH, turnOffScreen);
  }

  void partial_refresh(const uint8_t* new_pixels, const uint8_t* prev_pixels) override {
    (void)prev_pixels;  // the gray tint invalidated the RAM baseline; a full sync follows
    wakeIfNeeded();
    in_grayscale_mode_ = false;
    if (controller_ == Controller::UC8279) {
      display79_(new_pixels, EPD_FAST_REFRESH, false);
      return;
    }
    display53_(new_pixels, EPD_FAST_REFRESH, false);
  }

  // Grayscale staging. DrawBuffer's UC path sends masks, not bit-planes:
  //   write_ram_bw  = pixels to pull to DARK gray  -> 0x10
  //   write_ram_red = pixels to pull to ANY gray   -> 0x13
  void write_ram_bw(const uint8_t* data) override {
    wakeIfNeeded();
    waitIdle_();
    if (controller_ == Controller::UC8279)
      fullWindowIn79_();
    sendPlaneMirrored_(0x10, data, false);
    if (controller_ == Controller::UC8279) {
      sendCommand(0x11);  // data stop
      sendCommand(0x92);  // partial out
    }
  }

  void write_ram_red(const uint8_t* data) override {
    waitIdle_();
    if (controller_ == Controller::UC8279)
      fullWindowIn79_();
    sendPlaneMirrored_(0x13, data, false);
    if (controller_ == Controller::UC8279) {
      sendCommand(0x11);
      sendCommand(0x92);
    }
  }

  void grayscale_refresh(bool turnOffScreen = false) override {
    grayscale_refresh_1pass(turnOffScreen);
  }

  void grayscale_refresh_1pass(bool turnOffScreen = false) override {
    waitIdle_();
    if (controller_ == Controller::UC8279) {
      fullWindowIn79_();
      for (int t = 0; t < 5; ++t) {
        sendCommand(static_cast<uint8_t>(0x20 + t));
        sendData(x3lut::k79Gray[t], sizeof(x3lut::k79Gray[t]));
      }
      sendCommand(0x50);
      sendData(firstRefresh79_ ? x3lut::k79CdiFirst : x3lut::k79CdiLater);
      powerOnAndRefresh_(turnOffScreen);
      sendCommand(0x92);
      firstRefresh79_ = false;
      oldPlaneValid79_ = false;
    } else {
      loadBank53_(Bank::Gray);
      sendCmd2_(0x50, 0x29, 0x07);
      powerOnAndRefresh_(turnOffScreen);
      redRamSynced_ = false;  // the next B&W refresh starts with a full sync
      lutBank_ = Bank::None;
    }
    in_grayscale_mode_ = true;
  }

  void revert_grayscale(const uint8_t* prev_pixels) override {
    if (!in_grayscale_mode_)
      return;
    in_grayscale_mode_ = false;
    // Put the pre-grayscale frame back into both RAMs; the next refresh
    // (forced to a full sync) clears any gray tint left on screen.
    waitIdle_();
    if (controller_ == Controller::UC8279) {
      fullWindowIn79_();
      sendPlaneMirrored_(0x13, prev_pixels, false);
      sendCommand(0x11);
      sendPlaneMirrored_(0x10, prev_pixels, false);
      sendCommand(0x11);
      sendCommand(0x92);
      oldPlaneValid79_ = false;  // still force a clean pass
    } else {
      sendPlaneMirrored_(0x13, prev_pixels, false);
      sendPlaneMirrored_(0x10, prev_pixels, false);
      redRamSynced_ = false;
    }
  }

  // The "Converting..." progress box needs sub-rectangle updates, which this
  // port doesn't do yet; conversion still works, just without the box.
  void partial_refresh_region(int, int, int, int, const uint8_t*, int) override {}

  void deep_sleep() override {
    waitIdle_();
    if (isScreenOn) {
      sendCommand(0x02);  // power off
      waitBusy_("power-off");
      isScreenOn = false;
    }
    sendCommand(0x07);
    sendData(0xA5);
    inDeepSleep_ = true;
  }

 private:
  static constexpr const char* kTag = "epd_x3";

  enum class Bank : uint8_t { None, Full, Img, Turbo, Half, Gray };

  Controller controller_ = Controller::UC8253;
  spi_device_handle_t spi_ = nullptr;
  Bank lutBank_ = Bank::None;
  bool redRamSynced_ = false;      // UC8253: 0x10 holds the displayed frame
  uint8_t initialFullSyncs_ = 1;   // UC8253: first refresh after init is a full sync
  bool firstRefresh79_ = true;     // UC8279: first refresh uses the stronger CDI
  bool oldPlaneValid79_ = false;   // UC8279: 0x10 holds the displayed frame

  static void delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
  }
  static uint32_t millis() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
  }

  // ---- Controller probe (stock-firmware protocol, ported from PapyriX) ----
  // Bit-bangs a read of the version register before SPI is configured.
  static Controller probe_controller_() {
    gpio_reset_pin(EPD_SCLK);
    gpio_reset_pin(EPD_MOSI);
    gpio_set_direction(EPD_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(EPD_CS, 1);
    gpio_set_direction(EPD_SCLK, GPIO_MODE_OUTPUT);
    gpio_set_level(EPD_SCLK, 0);
    gpio_set_direction(EPD_DC, GPIO_MODE_OUTPUT);
    gpio_set_level(EPD_DC, 1);
    gpio_set_direction(EPD_MOSI, GPIO_MODE_OUTPUT);
    gpio_set_direction(EPD_BUSY, GPIO_MODE_INPUT);

    gpio_set_direction(EPD_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(EPD_RST, 1);
    delay(10);
    gpio_set_level(EPD_RST, 0);
    delay(50);
    gpio_set_level(EPD_RST, 1);
    delay(50);
    const uint32_t start = millis();
    while (gpio_get_level(EPD_BUSY) == 0 && millis() - start < 300)
      delay(1);

    auto clk = [] { esp_rom_delay_us(1); };
    // Send command 0x70 (read version).
    gpio_set_level(EPD_CS, 0);
    gpio_set_level(EPD_DC, 0);
    uint8_t cmd = 0x70;
    for (int bit = 0; bit < 8; ++bit) {
      gpio_set_level(EPD_MOSI, (cmd & 0x80) ? 1 : 0);
      clk();
      gpio_set_level(EPD_SCLK, 1);
      clk();
      gpio_set_level(EPD_SCLK, 0);
      cmd = static_cast<uint8_t>(cmd << 1);
    }
    gpio_set_level(EPD_DC, 1);
    esp_rom_delay_us(1);
    gpio_set_direction(EPD_MOSI, GPIO_MODE_INPUT);
    gpio_set_pull_mode(EPD_MOSI, GPIO_PULLUP_ONLY);
    esp_rom_delay_us(2);
    uint8_t ver[7] = {};
    for (auto& byte : ver) {
      for (int bit = 0; bit < 8; ++bit) {
        gpio_set_level(EPD_SCLK, 0);
        esp_rom_delay_us(1);
        gpio_set_level(EPD_SCLK, 1);
        esp_rom_delay_us(1);
        byte = static_cast<uint8_t>((byte << 1) | (gpio_get_level(EPD_MOSI) ? 1 : 0));
      }
      gpio_set_level(EPD_SCLK, 0);
      esp_rom_delay_us(1);
    }
    gpio_set_level(EPD_CS, 1);
    gpio_set_pull_mode(EPD_MOSI, GPIO_FLOATING);
    gpio_set_direction(EPD_MOSI, GPIO_MODE_OUTPUT);

    ESP_LOGI(kTag, "display probe: ver=%02X %02X %02X %02X %02X %02X %02X", ver[0], ver[1], ver[2], ver[3], ver[4],
             ver[5], ver[6]);
    if (ver[2] == 0x66)
      return Controller::UC8279;
    if (ver[2] != 0xFF)
      ESP_LOGW(kTag, "probe inconclusive; assuming UC8253");
    return Controller::UC8253;
  }

  // ---- SPI plumbing (manual CS/DC like epd.h) ----

  void spiWrite_(const uint8_t* data, size_t len) {
    while (len > 0) {
      const size_t n = len > 4000 ? 4000 : len;
      spi_transaction_t t{};
      t.length = n * 8;
      t.tx_buffer = data;
      spi_device_polling_transmit(spi_, &t);
      data += n;
      len -= n;
    }
  }

  void sendCommand(uint8_t cmd) {
    gpio_set_level(EPD_DC, 0);
    gpio_set_level(EPD_CS, 0);
    spiWrite_(&cmd, 1);
    gpio_set_level(EPD_CS, 1);
    gpio_set_level(EPD_DC, 1);
  }

  void sendData(const uint8_t* data, size_t len) {
    gpio_set_level(EPD_DC, 1);
    gpio_set_level(EPD_CS, 0);
    spiWrite_(data, len);
    gpio_set_level(EPD_CS, 1);
  }

  void sendData(uint8_t v) {
    sendData(&v, 1);
  }

  void sendCmdData_(uint8_t cmd, const uint8_t* data, size_t len) {
    sendCommand(cmd);
    sendData(data, len);
  }

  void sendCmd2_(uint8_t cmd, uint8_t d0, uint8_t d1) {
    const uint8_t d[2] = {d0, d1};
    sendCmdData_(cmd, d, 2);
  }

  // The panel scans rows in the opposite order from our frame buffer, so
  // planes go out with their rows mirrored (row 527 first).
  void sendPlaneMirrored_(uint8_t cmd, const uint8_t* plane, bool invert) {
    sendCommand(cmd);
    gpio_set_level(EPD_DC, 1);
    gpio_set_level(EPD_CS, 0);
    uint8_t row[DISPLAY_WIDTH_BYTES];
    for (int y = DISPLAY_HEIGHT - 1; y >= 0; --y) {
      const uint8_t* src = plane + static_cast<uint32_t>(y) * DISPLAY_WIDTH_BYTES;
      if (invert) {
        for (int i = 0; i < DISPLAY_WIDTH_BYTES; ++i)
          row[i] = static_cast<uint8_t>(~src[i]);
        spiWrite_(row, DISPLAY_WIDTH_BYTES);
      } else {
        spiWrite_(src, DISPLAY_WIDTH_BYTES);
      }
    }
    gpio_set_level(EPD_CS, 1);
  }

  void fillPlane_(uint8_t cmd, uint8_t value) {
    sendCommand(cmd);
    gpio_set_level(EPD_DC, 1);
    gpio_set_level(EPD_CS, 0);
    uint8_t row[DISPLAY_WIDTH_BYTES];
    memset(row, value, sizeof(row));
    for (int y = 0; y < DISPLAY_HEIGHT; ++y)
      spiWrite_(row, DISPLAY_WIDTH_BYTES);
    gpio_set_level(EPD_CS, 1);
  }

  // ---- BUSY handling: LOW while working, HIGH when idle ----

  // Wait for a triggered operation: BUSY asserts (goes LOW), then completes.
  bool waitBusy_(const char* what) {
    const uint32_t start = millis();
    while (gpio_get_level(EPD_BUSY) == 1) {
      delay(1);
      if (millis() - start > 1000)
        break;
    }
    if (gpio_get_level(EPD_BUSY) == 1) {
      ESP_LOGW(kTag, "BUSY did not assert for %s", what);
      redRamSynced_ = false;
      oldPlaneValid79_ = false;
      lutBank_ = Bank::None;
      return false;
    }
    while (gpio_get_level(EPD_BUSY) == 0) {
      delay(1);
      if (millis() - start > 30000) {
        ESP_LOGE(kTag, "BUSY stuck for %s", what);
        redRamSynced_ = false;
        oldPlaneValid79_ = false;
        lutBank_ = Bank::None;
        return false;
      }
    }
    return true;
  }

  // Wait only until the controller is idle (no assertion expected).
  void waitIdle_() {
    const uint32_t start = millis();
    while (gpio_get_level(EPD_BUSY) == 0 && millis() - start < 30000)
      delay(1);
  }

  bool powerOnAndRefresh_(bool turnOffScreen) {
    if (!isScreenOn) {
      sendCommand(0x04);
      if (!waitBusy_("power-on"))
        return false;
      isScreenOn = true;
    }
    sendCommand(0x12);
    if (!waitBusy_("refresh"))
      return false;
    if (turnOffScreen) {
      sendCommand(0x02);
      if (!waitBusy_("power-off"))
        return false;
      isScreenOn = false;
    }
    return true;
  }

  // ---- init / reset ----

  void resetDisplay() {
    gpio_set_level(EPD_RST, 1);
    delay(20);
    gpio_set_level(EPD_RST, 0);
    delay(10);
    gpio_set_level(EPD_RST, 1);
    delay(20);
    delay(50);
  }

  void initDisplayController(bool /*clearBuffer*/) {
    if (controller_ == Controller::UC8279) {
      const uint8_t* script = x3lut::k79Init;
      size_t off = 0;
      while (off + 2 <= sizeof(x3lut::k79Init)) {
        const uint8_t cmd = script[off++];
        const uint8_t len = script[off++];
        sendCommand(cmd);
        if (len > 0)
          sendData(script + off, len);
        off += len;
      }
      firstRefresh79_ = true;
      oldPlaneValid79_ = false;
      initialFullSyncs79_ = 2;
    } else {
      static const uint8_t kPsr[] = {0x3F, 0x08};
      static const uint8_t kRes[] = {0x03, 0x18, 0x02, 0x58};  // 792 x 600 (528 rows used)
      static const uint8_t kFlash[] = {0x00, 0x00, 0x00, 0x00};
      static const uint8_t kPwr[] = {0x07, 0x17, 0x3F, 0x3F, 0x17};
      static const uint8_t kBoost[] = {0x25, 0x25, 0x3C, 0x37};
      sendCmdData_(0x00, kPsr, sizeof(kPsr));
      sendCmdData_(0x61, kRes, sizeof(kRes));
      sendCmdData_(0x65, kFlash, sizeof(kFlash));
      sendCommand(0x03);
      sendData(0x1D);
      sendCmdData_(0x01, kPwr, sizeof(kPwr));
      sendCommand(0x82);
      sendData(0x1D);
      sendCmdData_(0x06, kBoost, sizeof(kBoost));
      sendCommand(0x30);
      sendData(0x09);
      sendCommand(0xE1);
      sendData(0x02);
      loadBank53_(Bank::Full);
      redRamSynced_ = false;
      initialFullSyncs_ = 1;
    }
    isScreenOn = false;
    inDeepSleep_ = false;
    in_grayscale_mode_ = false;
  }

  void wakeIfNeeded() {
    if (!inDeepSleep_)
      return;
    resetDisplay();
    initDisplayController(false);
  }

  // ---- UC8253 ----

  void loadBank53_(Bank bank) {
    if (lutBank_ == bank)
      return;
    const uint8_t* t[5] = {};
    switch (bank) {
      case Bank::Full:
        t[0] = x3lut::kVcomFull; t[1] = x3lut::kWwFull; t[2] = x3lut::kBwFull; t[3] = x3lut::kWbFull; t[4] = x3lut::kBbFull;
        break;
      case Bank::Img:
        t[0] = x3lut::kVcomImg; t[1] = x3lut::kWwImg; t[2] = x3lut::kBwImg; t[3] = x3lut::kWbImg; t[4] = x3lut::kBbImg;
        break;
      case Bank::Turbo:
        t[0] = x3lut::kVcomTurbo; t[1] = x3lut::kWwTurbo; t[2] = x3lut::kBwTurbo; t[3] = x3lut::kWbTurbo; t[4] = x3lut::kBbTurbo;
        break;
      case Bank::Half:
        t[0] = x3lut::kVcomHalf; t[1] = x3lut::kWwHalf; t[2] = x3lut::kBwHalf; t[3] = x3lut::kWbHalf; t[4] = x3lut::kBbHalf;
        break;
      case Bank::Gray:
        t[0] = x3lut::kVcomGray; t[1] = x3lut::kWwGray; t[2] = x3lut::kBwGray; t[3] = x3lut::kWbGray; t[4] = x3lut::kBbGray;
        break;
      case Bank::None:
        return;
    }
    for (int i = 0; i < 5; ++i)
      sendCmdData_(static_cast<uint8_t>(0x20 + i), t[i], 42);
    lutBank_ = bank;
  }

  void display53_(const uint8_t* frame, EpdRefreshMode mode, bool turnOffScreen) {
    waitIdle_();
    // A fast diff needs a valid previous frame in 0x10 and a powered panel.
    const bool fullSync = mode == EPD_FULL_REFRESH || !redRamSynced_ || initialFullSyncs_ > 0;
    const bool halfSync = !fullSync && (mode == EPD_HALF_REFRESH || !isScreenOn);

    const uint32_t t0 = millis();
    if (fullSync) {
      // Both RAMs get the inverted frame; the "img" waveform renders it.
      loadBank53_(Bank::Img);
      sendPlaneMirrored_(0x13, frame, true);
      sendPlaneMirrored_(0x10, frame, true);
      sendCmd2_(0x50, 0xA9, 0x07);
    } else if (halfSync) {
      // State-independent scrub of the new frame.
      loadBank53_(Bank::Half);
      sendPlaneMirrored_(0x13, frame, false);
      sendCmd2_(0x50, 0xA9, 0x07);
    } else {
      // Differential page turn: the controller compares 0x13 against 0x10.
      loadBank53_(Bank::Turbo);
      sendPlaneMirrored_(0x13, frame, false);
      sendCmd2_(0x50, 0x29, 0x07);
    }

    if (!powerOnAndRefresh_(turnOffScreen && !fullSync))
      return;
    if (mode != EPD_FAST_REFRESH)
      delay(200);

    // One light conditioning pass after the first full sync of a session.
    if (fullSync && initialFullSyncs_ == 1) {
      loadBank53_(Bank::Full);
      sendCmd2_(0x50, 0x29, 0x07);
      sendCommand(0x91);
      sendCmdData_(0x90, x3lut::k79FullWindow, 9);
      sendPlaneMirrored_(0x13, frame, false);
      sendCommand(0x92);
      if (!powerOnAndRefresh_(false))
        return;
    }

    // 0x10 keeps the displayed frame for the next differential update.
    sendPlaneMirrored_(0x10, frame, false);
    redRamSynced_ = true;

    if (fullSync) {
      // Settle pass: rewrite the frame non-inverted so the panel and both
      // RAMs agree, then refresh once with the turbo waveform.
      loadBank53_(Bank::Turbo);
      sendCmd2_(0x50, 0x29, 0x07);
      sendPlaneMirrored_(0x13, frame, false);
      if (!powerOnAndRefresh_(turnOffScreen))
        return;
    }

    if (fullSync && initialFullSyncs_ > 0)
      --initialFullSyncs_;
    ESP_LOGI(kTag, "refresh %s took %lums", fullSync ? "FULL" : halfSync ? "HALF" : "FAST",
             static_cast<unsigned long>(millis() - t0));
  }

  // ---- UC8279 ----

  uint8_t initialFullSyncs79_ = 2;

  void fullWindowIn79_() {
    sendCommand(0x91);
    sendCmdData_(0x90, x3lut::k79FullWindow, 9);
  }

  void loadBank79_(const uint8_t (*bank)[43]) {
    for (int t = 0; t < 5; ++t) {
      sendCommand(bank[t][0]);
      sendData(&bank[t][1], 42);
    }
  }

  void display79_(const uint8_t* frame, EpdRefreshMode mode, bool turnOffScreen) {
    waitIdle_();
    const bool useGc = mode != EPD_FAST_REFRESH || !oldPlaneValid79_ || initialFullSyncs79_ > 0;

    const uint32_t t0 = millis();
    sendCommand(0x91);
    if (!oldPlaneValid79_) {
      fillPlane_(0x10, 0xFF);
      sendCommand(0x11);
    }
    sendPlaneMirrored_(0x13, frame, false);
    sendCommand(0x11);

    sendCommand(0x50);
    sendData(firstRefresh79_ ? x3lut::k79CdiFirst : x3lut::k79CdiLater);
    loadBank79_(useGc ? x3lut::k79Gc : x3lut::k79Du);

    if (!powerOnAndRefresh_(false))
      return;

    sendCommand(0x50);
    sendData(x3lut::k79CdiLater);
    sendPlaneMirrored_(0x10, frame, false);
    sendCommand(0x11);
    sendCommand(0x92);

    oldPlaneValid79_ = true;
    firstRefresh79_ = false;
    if (useGc && initialFullSyncs79_ > 0)
      --initialFullSyncs79_;

    if (turnOffScreen) {
      sendCommand(0x02);
      if (!waitBusy_("power-off"))
        return;
      isScreenOn = false;
    }
    ESP_LOGI(kTag, "refresh %s took %lums", useGc ? "GC" : "DU", static_cast<unsigned long>(millis() - t0));
  }
};
