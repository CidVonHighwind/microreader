#ifndef SDCARD_MANAGER_H
#define SDCARD_MANAGER_H

#include <Arduino.h>

#include <vector>

class SDCardManager {
 public:
  SDCardManager(uint8_t epd_sclk, uint8_t sd_miso, uint8_t epd_mosi, uint8_t sd_cs, uint8_t eink_cs);
  bool begin();
  bool ready() const;
  std::vector<String> listFiles(const char* path = "/", int maxFiles = 200);
  // Read the entire file at `path` into a String. Returns empty string on failure.
  String readFile(const char* path);

 private:
  uint8_t epd_sclk;
  uint8_t sd_miso;
  uint8_t epd_mosi;
  uint8_t sd_cs;
  uint8_t eink_cs;
  bool initialized = false;
};

#endif
