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

#include "EInk_Base.h"

#if defined(ESP8266) || defined(ESP32)
#include <pgmspace.h>
#else
#include <avr/pgmspace.h>
#endif

EInk_Base::EInk_Base(int16_t cs, int16_t dc, int16_t rst, int16_t busy, int16_t busy_level, uint32_t busy_timeout,
                     uint16_t w, uint16_t h, EInk::Panel p, bool c, bool pu, bool fpu)
    : WIDTH(w),
      HEIGHT(h),
      panel(p),
      hasColor(c),
      hasPartialUpdate(pu),
      hasFastPartialUpdate(fpu),
      _cs(cs),
      _dc(dc),
      _rst(rst),
      _busy(busy),
      _busy_level(busy_level),
      _busy_timeout(busy_timeout),
      _diag_enabled(false),
      _pSPIx(&SPI),
      _spi_settings(4000000, MSBFIRST, SPI_MODE0) {
  _initial_write = true;
  _initial_refresh = true;
  _power_is_on = false;
  _using_partial_mode = false;
  _hibernating = false;
  _init_display_done = false;
  _reset_duration = 10;
  _busy_callback = 0;
  _busy_callback_parameter = 0;
}

void EInk_Base::init(uint32_t serial_diag_bitrate) {
  init(serial_diag_bitrate, true, 10, false);
}

void EInk_Base::init(uint32_t serial_diag_bitrate, bool initial, uint16_t reset_duration, bool pulldown_rst_mode) {
  _initial_write = initial;
  _initial_refresh = initial;
  _pulldown_rst_mode = pulldown_rst_mode;
  _power_is_on = false;
  _using_partial_mode = false;
  _hibernating = false;
  _init_display_done = false;
  _reset_duration = reset_duration;
  if (serial_diag_bitrate > 0) {
    Serial.begin(serial_diag_bitrate);
    _diag_enabled = true;
  }
  if (_cs >= 0) {
    digitalWrite(_cs, HIGH);  // preset (less glitch for any analyzer)
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);  // set (needed e.g. for RP2040)
  }
  _reset();
  _pSPIx->begin();  // may steal _rst pin (Waveshare Pico-ePaper-2.9)
  if (_rst >= 0) {
    digitalWrite(_rst, HIGH);  // preset (less glitch for any analyzer)
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, HIGH);  // set (needed e.g. for RP2040)
  }
  if (_cs >= 0) {
    digitalWrite(_cs, HIGH);  // preset (less glitch for any analyzer)
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);  // set (needed e.g. for RP2040)
  }
  if (_dc >= 0) {
    digitalWrite(_dc, HIGH);  // preset (less glitch for any analyzer)
    pinMode(_dc, OUTPUT);
    digitalWrite(_dc, HIGH);  // set (needed e.g. for RP2040)
  }
  if (_busy >= 0) {
    pinMode(_busy, INPUT);
  }
}

void EInk_Base::end() {
  _pSPIx->end();
  if (_cs >= 0)
    pinMode(_cs, INPUT);
  if (_dc >= 0)
    pinMode(_dc, INPUT);
  if (_rst >= 0)
    pinMode(_rst, INPUT);
}

void EInk_Base::setBusyCallback(void (*busyCallback)(const void*), const void* busy_callback_parameter) {
  _busy_callback = busyCallback;
  _busy_callback_parameter = busy_callback_parameter;
}

void EInk_Base::selectSPI(SPIClass& spi, SPISettings spi_settings) {
  _pSPIx = &spi;
  _spi_settings = spi_settings;
}

void EInk_Base::_reset() {
  if (_rst >= 0) {
    if (_pulldown_rst_mode) {
      digitalWrite(_rst, LOW);
      pinMode(_rst, OUTPUT);
      digitalWrite(_rst, LOW);
      delay(_reset_duration);
      pinMode(_rst, INPUT_PULLUP);
      delay(_reset_duration > 10 ? _reset_duration : 10);
    } else {
      digitalWrite(_rst, HIGH);  // NEEDED for Waveshare "clever" reset circuit, power controller before reset pulse,
                                 // preset (less glitch for any analyzer)
      pinMode(_rst, OUTPUT);
      digitalWrite(_rst, HIGH);  // NEEDED for Waveshare "clever" reset circuit, power controller before reset pulse,
                                 // set (needed e.g. for RP2040)
      delay(10);                 // NEEDED for Waveshare "clever" reset circuit, at least delay(2);
      digitalWrite(_rst, LOW);
      delay(_reset_duration);
      digitalWrite(_rst, HIGH);
      delay(_reset_duration > 10 ? _reset_duration : 10);
    }
    _hibernating = false;
  }
}

unsigned long EInk_Base::_waitWhileBusy(const char* comment, uint16_t busy_time) {
  unsigned long elapsed = 0;
  if (_busy >= 0) {
    delay(1);  // add some margin to become active
    unsigned long start = micros();
    while (1) {
      if (digitalRead(_busy) != _busy_level)
        break;
      if (_busy_callback)
        _busy_callback(_busy_callback_parameter);
      else
        delay(1);
      if (digitalRead(_busy) != _busy_level)
        break;
      if (micros() - start > _busy_timeout) {
        Serial.println("Busy Timeout!");
        break;
      }
#if defined(ESP8266) || defined(ESP32)
      yield();  // avoid wdt
#endif
    }
    elapsed = micros() - start;
    if (comment) {
#if !defined(DISABLE_DIAGNOSTIC_OUTPUT)
      if (_diag_enabled) {
        Serial.print(comment);
        Serial.print(" : ");
        Serial.println(elapsed);
      }
#endif
    }
  } else {
    delay(busy_time);
    elapsed = (unsigned long)busy_time * 1000;  // Convert ms to us
  }
  return elapsed;
}

void EInk_Base::_writeCommand(uint8_t c) {
  _pSPIx->beginTransaction(_spi_settings);
  if (_dc >= 0)
    digitalWrite(_dc, LOW);
  if (_cs >= 0)
    digitalWrite(_cs, LOW);
  _pSPIx->transfer(c);
  if (_cs >= 0)
    digitalWrite(_cs, HIGH);
  if (_dc >= 0)
    digitalWrite(_dc, HIGH);
  _pSPIx->endTransaction();
}

void EInk_Base::_writeData(uint8_t d) {
  _pSPIx->beginTransaction(_spi_settings);
  if (_cs >= 0)
    digitalWrite(_cs, LOW);
  _pSPIx->transfer(d);
  if (_cs >= 0)
    digitalWrite(_cs, HIGH);
  _pSPIx->endTransaction();
}

void EInk_Base::_writeData(const uint8_t* data, uint16_t n) {
  _pSPIx->beginTransaction(_spi_settings);
  if (_cs >= 0)
    digitalWrite(_cs, LOW);
  for (uint16_t i = 0; i < n; i++) {
    _pSPIx->transfer(*data++);
  }
  if (_cs >= 0)
    digitalWrite(_cs, HIGH);
  _pSPIx->endTransaction();
}

void EInk_Base::_writeDataPGM(const uint8_t* data, uint16_t n, int16_t fill_with_zeroes) {
  _pSPIx->beginTransaction(_spi_settings);
  if (_cs >= 0)
    digitalWrite(_cs, LOW);
  for (uint16_t i = 0; i < n; i++) {
    _pSPIx->transfer(pgm_read_byte(&*data++));
  }
  while (fill_with_zeroes > 0) {
    _pSPIx->transfer(0x00);
    fill_with_zeroes--;
  }
  if (_cs >= 0)
    digitalWrite(_cs, HIGH);
  _pSPIx->endTransaction();
}

void EInk_Base::_writeDataPGM_sCS(const uint8_t* data, uint16_t n, int16_t fill_with_zeroes) {
  _pSPIx->beginTransaction(_spi_settings);
  for (uint8_t i = 0; i < n; i++) {
    if (_cs >= 0)
      digitalWrite(_cs, LOW);
    _pSPIx->transfer(pgm_read_byte(&*data++));
    if (_cs >= 0)
      digitalWrite(_cs, HIGH);
  }
  while (fill_with_zeroes > 0) {
    if (_cs >= 0)
      digitalWrite(_cs, LOW);
    _pSPIx->transfer(0x00);
    fill_with_zeroes--;
    if (_cs >= 0)
      digitalWrite(_cs, HIGH);
  }
  _pSPIx->endTransaction();
}

void EInk_Base::_writeCommandData(const uint8_t* pCommandData, uint8_t datalen) {
  _pSPIx->beginTransaction(_spi_settings);
  if (_dc >= 0)
    digitalWrite(_dc, LOW);
  if (_cs >= 0)
    digitalWrite(_cs, LOW);
  _pSPIx->transfer(*pCommandData++);
  if (_dc >= 0)
    digitalWrite(_dc, HIGH);
  for (uint8_t i = 0; i < datalen - 1; i++)  // sub the command
  {
    _pSPIx->transfer(*pCommandData++);
  }
  if (_cs >= 0)
    digitalWrite(_cs, HIGH);
  _pSPIx->endTransaction();
}

void EInk_Base::_writeCommandDataPGM(const uint8_t* pCommandData, uint8_t datalen) {
  _pSPIx->beginTransaction(_spi_settings);
  if (_dc >= 0)
    digitalWrite(_dc, LOW);
  if (_cs >= 0)
    digitalWrite(_cs, LOW);
  _pSPIx->transfer(pgm_read_byte(&*pCommandData++));
  if (_dc >= 0)
    digitalWrite(_dc, HIGH);
  for (uint8_t i = 0; i < datalen - 1; i++)  // sub the command
  {
    _pSPIx->transfer(pgm_read_byte(&*pCommandData++));
  }
  if (_cs >= 0)
    digitalWrite(_cs, HIGH);
  _pSPIx->endTransaction();
}

void EInk_Base::_startTransfer() {
  _pSPIx->beginTransaction(_spi_settings);
  if (_cs >= 0)
    digitalWrite(_cs, LOW);
}

void EInk_Base::_transfer(uint8_t value) {
  _pSPIx->transfer(value);
}

void EInk_Base::_endTransfer() {
  if (_cs >= 0)
    digitalWrite(_cs, HIGH);
  _pSPIx->endTransaction();
}
