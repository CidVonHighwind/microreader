#ifndef FILE_WORD_PROVIDER_H
#define FILE_WORD_PROVIDER_H

#include <SD.h>

#include <cstdint>

#include "WordProvider.h"

class FileWordProvider : public WordProvider {
 public:
  // path: SD path to text file
  // bufSize: internal sliding window buffer size in bytes (default 2048)
  FileWordProvider(const char* path, size_t bufSize = 2048);
  ~FileWordProvider() override;
  bool isValid() const {
    return file_;
  }

  bool hasNextWord() override;
  bool hasPrevWord() override;
  String getNextWord() override;
  String getPrevWord() override;

  float getPercentage() override;
  float getPercentage(int index) override;
  void setPosition(int index) override;
  int getCurrentIndex() override;
  char peekChar(int offset = 0) override;
  int consumeChars(int n) override;
  bool isInsideWord() override;
  void ungetWord() override;
  void reset() override;

 private:
  String scanWord(int direction);

  bool ensureBufferForPos(size_t pos);
  char charAt(size_t pos);

  File file_;
  size_t fileSize_ = 0;
  size_t index_ = 0;
  size_t prevIndex_ = 0;

  uint8_t* buf_ = nullptr;
  size_t bufSize_ = 0;
  size_t bufStart_ = 0;  // file offset of buf_[0]
  size_t bufLen_ = 0;    // valid bytes in buf_
};

#endif
