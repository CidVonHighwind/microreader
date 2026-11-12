#pragma once

#include <sys/time.h>

#include <algorithm>
#include <cmath>
#include <ctime>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/adc_types.h"
#include "microreader/Input.h"
#include "microreader/Runtime.h"

// Assuming battery is connected to ADC1 channel for GPIO0
// Usually GPIO0 maps to ADC1_CHANNEL_0 on ESP32-C3
// But you may need to adjust the exact channel or unit if different
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0

#ifdef MICROREADER_X3
// The X3 has no battery voltage divider; a BQ27220 fuel gauge reports the
// state of charge over I2C (SDA = GPIO20, SCL = GPIO0, address 0x55).
#include "driver/i2c_master.h"
#endif

class Esp32Runtime final : public microreader::IRuntime {
 public:
  explicit Esp32Runtime(uint32_t frame_time_ms, adc_oneshot_unit_handle_t adc_handle)
      : frame_time_ms_(frame_time_ms), frame_start_ms_(0), adc1_handle_(adc_handle) {
#ifndef MICROREADER_X3
    init_battery_adc();  // X3: GPIO0 is the fuel gauge's I2C clock, not an ADC pin
#endif
  }

  ~Esp32Runtime() override {
    if (adc_cali_handle_) {
      adc_cali_delete_scheme_curve_fitting(adc_cali_handle_);
    }
  }

  bool should_continue() const override {
    return true;
  }

  uint32_t frame_time_ms() const override {
    return frame_time_ms_;
  }

  void wait_next_frame() override {
    const uint32_t now = millis();
    if (frame_start_ms_ != 0) {
      const uint32_t elapsed = now - frame_start_ms_;
      if (elapsed < frame_time_ms_)
        vTaskDelay(pdMS_TO_TICKS(frame_time_ms_ - elapsed));
      else
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    frame_start_ms_ = millis();
  }

  std::optional<uint8_t> battery_percentage() const override {
#ifdef MICROREADER_X3
    return bq27220_percentage_();
#else
    return adc_percentage_();
#endif
  }

#ifdef MICROREADER_X3
  std::optional<uint8_t> bq27220_percentage_() const {
    // The gauge keeps its own reading; polling more than every few seconds
    // only adds I2C traffic.
    const uint32_t now = millis();
    if (last_pct_.has_value() && now - last_gauge_poll_ms_ < 5000)
      return last_pct_;
    if (!gauge_ && !init_gauge_())
      return last_pct_;
    uint8_t reg = 0x2C;  // StateOfCharge(), 16-bit little endian, percent
    uint8_t raw[2] = {};
    if (i2c_master_transmit_receive(gauge_, &reg, 1, raw, 2, 50) != ESP_OK)
      return last_pct_;
    const uint16_t soc = static_cast<uint16_t>(raw[0] | (raw[1] << 8));
    if (soc > 100)
      return last_pct_;
    last_gauge_poll_ms_ = now;
    last_pct_ = static_cast<uint8_t>(soc);
    return last_pct_;
  }

  bool init_gauge_() const {
    i2c_master_bus_config_t bus{};
    bus.i2c_port = I2C_NUM_0;
    bus.sda_io_num = GPIO_NUM_20;
    bus.scl_io_num = GPIO_NUM_0;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bus, &gauge_bus_) != ESP_OK) {
      gauge_bus_ = nullptr;
      return false;
    }
    i2c_device_config_t dev{};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = 0x55;
    dev.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(gauge_bus_, &dev, &gauge_) != ESP_OK) {
      gauge_ = nullptr;
      return false;
    }
    return true;
  }
#endif

  std::optional<uint8_t> adc_percentage_() const {
    if (!adc1_handle_)
      return std::nullopt;

    int adc_raw = 0;
    if (adc_oneshot_read(adc1_handle_, BATTERY_ADC_CHANNEL, &adc_raw) != ESP_OK) {
      return std::nullopt;
    }

    int voltage_mv = 0;
    if (adc_cali_handle_) {
      adc_cali_raw_to_voltage(adc_cali_handle_, adc_raw, &voltage_mv);
    } else {
      // Fallback or handle differently if uncalibrated
      voltage_mv = adc_raw;
    }

    float millivolts = voltage_mv * 2.0f;  // Voltage divider multiplier

    double volts = millivolts / 1000.0;
    // Polynomial derived from LiPo samples
    double y = -144.9390 * volts * volts * volts + 1655.8629 * volts * volts - 6158.8520 * volts + 7501.3202;

    // Clamp to [0,100] and round
    y = std::max(y, 0.0);
    y = std::min(y, 100.0);
    y = std::round(y);

    // Hysteresis: only update the displayed value when the new reading differs
    // by more than kHysteresisPercent from the last displayed value.  This
    // prevents voltage noise causing the indicator to flicker between adjacent
    // percentages when switching screens.
    const int new_pct = static_cast<int>(y);
    if (!last_pct_.has_value() || std::abs(new_pct - static_cast<int>(last_pct_.value())) >= kHysteresisPercent) {
      last_pct_ = static_cast<uint8_t>(new_pct);
    }
    return last_pct_;
  }

  void yield() override {
    vTaskDelay(1);  // 1 tick (10ms at 100Hz); pdMS_TO_TICKS(1) rounds to 0
  }

  // Clocks for Reading Stats. The system clock holds LOCAL wall time (no TZ
  // configured). ESP-IDF keeps it running through deep sleep; after a cold
  // power-on it restarts at 1970 and Application restores the last saved value.
  uint32_t monotonic_ms() const override {
    return millis();
  }
  int64_t local_time() const override {
    return static_cast<int64_t>(time(nullptr));
  }
  bool set_local_time(int64_t seconds) override {
    struct timeval tv = {};
    tv.tv_sec = static_cast<time_t>(seconds);
    return settimeofday(&tv, nullptr) == 0;
  }

 private:
#ifdef MICROREADER_X3
  mutable i2c_master_bus_handle_t gauge_bus_ = nullptr;
  mutable i2c_master_dev_handle_t gauge_ = nullptr;
  mutable uint32_t last_gauge_poll_ms_ = 0;
#endif

  void init_battery_adc() {
    // Configuration for ESP32-C3 ADC1 Channel 0 (GPIO0)
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_oneshot_config_channel(adc1_handle_, BATTERY_ADC_CHANNEL, &config) != ESP_OK) {
      return;
    }

    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = BATTERY_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle_) != ESP_OK) {
      adc_cali_handle_ = nullptr;
    }
  }

  static uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000);
  }

  // Only update the displayed battery percentage when the reading has moved
  // at least this many percentage points away from the last displayed value.
  static constexpr int kHysteresisPercent = 3;

  uint32_t frame_time_ms_;
  uint32_t frame_start_ms_;
  adc_oneshot_unit_handle_t adc1_handle_ = nullptr;
  adc_cali_handle_t adc_cali_handle_ = nullptr;
  mutable std::optional<uint8_t> last_pct_;
};
