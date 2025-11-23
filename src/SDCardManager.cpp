#include "SDCardManager.h"

#include <SD.h>
#include <SPI.h>

SDCardManager::SDCardManager(uint8_t epd_sclk, uint8_t sd_miso, uint8_t epd_mosi, uint8_t sd_cs, uint8_t eink_cs)
    : epd_sclk(epd_sclk), sd_miso(sd_miso), epd_mosi(epd_mosi), sd_cs(sd_cs), eink_cs(eink_cs), initialized(false) {}

bool SDCardManager::begin() {
  pinMode(eink_cs, OUTPUT);
  digitalWrite(eink_cs, HIGH);

  pinMode(sd_cs, OUTPUT);
  digitalWrite(sd_cs, HIGH);

  SPI.begin(epd_sclk, sd_miso, epd_mosi, sd_cs);
  if (!SD.begin(sd_cs, SPI, 40000000)) {
    Serial.print("\n SD card not detected\n");
    initialized = false;
  } else {
    Serial.print("\n SD card detected\n");
    initialized = true;
  }

  return initialized;
}

bool SDCardManager::ready() const {
  return initialized;
}

std::vector<String> SDCardManager::listFiles(const char* path, int maxFiles) {
  std::vector<String> ret;
  if (!initialized) {
    Serial.println("SDCardManager: not initialized, returning empty list");
    return ret;
  }

  File root = SD.open(path);
  if (!root) {
    Serial.println("Failed to open directory.");
    return ret;
  }
  if (!root.isDirectory()) {
    Serial.println("Path is not a directory.");
    root.close();
    return ret;
  }

  int count = 0;
  for (File f = root.openNextFile(); f && count < maxFiles; f = root.openNextFile()) {
    if (f.isDirectory()) {
      f.close();
      continue;
    }
    ret.push_back(String(f.name()));
    f.close();
    count++;
  }
  root.close();
  return ret;
}
