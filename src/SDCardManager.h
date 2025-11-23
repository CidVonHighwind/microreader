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

 private:
  uint8_t epd_sclk;
  uint8_t sd_miso;
  uint8_t epd_mosi;
  uint8_t sd_cs;
  uint8_t eink_cs;
  bool initialized = false;
};

#endif
