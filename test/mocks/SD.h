#pragma once

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>

struct MockFile {
  std::string content;
  size_t currentPos = 0;
  bool isOpen = false;
  MockFile() {}
  ~MockFile() {
    close();
  }
  operator bool() const {
    return isOpen;
  }
  size_t size() {
    return content.size();
  }
  bool seek(size_t pos) {
    if (!isOpen)
      return false;
    currentPos = pos;
    return true;
  }
  size_t read(void* buf, size_t len) {
    if (!isOpen)
      return 0;
    size_t toRead = std::min(len, content.size() - currentPos);
    memcpy(buf, content.data() + currentPos, toRead);
    currentPos += toRead;
    return toRead;
  }
  int read() {
    if (!isOpen || currentPos >= content.size())
      return -1;
    return static_cast<unsigned char>(content[currentPos++]);
  }
  bool available() {
    return isOpen && currentPos < content.size();
  }
  void close() {
    isOpen = false;
    content.clear();
    currentPos = 0;
  }
};

struct MockSD {
  MockFile open(const char* path, int mode = 0) {
    MockFile f;
    std::ifstream in(path, std::ios::binary);
    if (in.is_open()) {
      f.isOpen = true;
      std::string& content = f.content;
      in.seekg(0, std::ios::end);
      content.resize(in.tellg());
      in.seekg(0, std::ios::beg);
      in.read(content.data(), content.size());
      in.close();
    }
    return f;
  }
  bool exists(const char* path) {
    std::ifstream in(path);
    return in.good();
  }
  bool mkdir(const char* path) {
    return true;  // Mock implementation
  }
};

extern MockSD SD;
typedef MockFile File;

// File open modes
#define FILE_READ 0
#define FILE_WRITE 1