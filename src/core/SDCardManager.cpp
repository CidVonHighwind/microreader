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

String SDCardManager::readFile(const char* path) {
  if (!initialized) {
    Serial.println("SDCardManager: not initialized; cannot read file");
    return String("");
  }

  File f = SD.open(path);
  if (!f) {
    Serial.printf("Failed to open file: %s\n", path);
    return String("");
  }

  String content = "";
  size_t maxSize = 50000;  // Limit to 50KB
  size_t readSize = 0;
  while (f.available() && readSize < maxSize) {
    char c = (char)f.read();
    content += c;
    readSize++;
  }
  f.close();
  return content;
}

bool SDCardManager::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  if (!initialized) {
    Serial.println("SDCardManager: not initialized; cannot read file");
    return false;
  }

  File f = SD.open(path);
  if (!f) {
    Serial.printf("Failed to open file: %s\n", path);
    return false;
  }

  const size_t localBufSize = 256;
  uint8_t buf[localBufSize];
  size_t toRead = (chunkSize == 0) ? localBufSize : (chunkSize < localBufSize ? chunkSize : localBufSize);

  while (f.available()) {
    int r = f.read(buf, toRead);
    if (r > 0) {
      out.write(buf, (size_t)r);
    } else {
      break;
    }
  }

  f.close();
  return true;
}

size_t SDCardManager::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  if (!buffer || bufferSize == 0)
    return 0;
  if (!initialized) {
    Serial.println("SDCardManager: not initialized; cannot read file");
    buffer[0] = '\0';
    return 0;
  }

  File f = SD.open(path);
  if (!f) {
    Serial.printf("Failed to open file: %s\n", path);
    buffer[0] = '\0';
    return 0;
  }

  size_t maxToRead = (maxBytes == 0) ? (bufferSize - 1) : min(maxBytes, bufferSize - 1);
  size_t total = 0;
  const size_t chunk = 64;

  while (f.available() && total < maxToRead) {
    size_t want = maxToRead - total;
    size_t readLen = (want < chunk) ? want : chunk;
    int r = f.read((uint8_t*)(buffer + total), readLen);
    if (r > 0) {
      total += (size_t)r;
    } else {
      break;
    }
  }

  buffer[total] = '\0';
  f.close();
  return total;
}

bool SDCardManager::writeFile(const char* path, const String& content) {
  if (!initialized) {
    Serial.println("SDCardManager: not initialized; cannot write file");
    return false;
  }

  // Remove existing file so we perform an overwrite rather than append
  if (SD.exists(path)) {
    SD.remove(path);
  }

  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("Failed to open file for write: %s\n", path);
    return false;
  }

  size_t written = f.print(content);
  f.close();
  return (written == content.length());
}

bool SDCardManager::ensureDirectoryExists(const char* path) {
  if (!initialized) {
    Serial.println("SDCardManager: not initialized; cannot create directory");
    return false;
  }

  // Check if directory already exists
  if (SD.exists(path)) {
    File dir = SD.open(path);
    if (dir && dir.isDirectory()) {
      dir.close();
      Serial.printf("Directory already exists: %s\n", path);
      return true;
    }
    dir.close();
  }

  // Create the directory
  if (SD.mkdir(path)) {
    Serial.printf("Created directory: %s\n", path);
    return true;
  } else {
    Serial.printf("Failed to create directory: %s\n", path);
    return false;
  }
}
