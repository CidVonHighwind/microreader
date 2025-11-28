#ifndef FILE_WORD_PROVIDER_H
#define FILE_WORD_PROVIDER_H

#include <SD.h>

#include "WordProvider.h"

class FileWordProvider : public WordProvider {
 public:
  // path: SD path to text file
  // bufSize: internal sliding window buffer size in bytes (default 2048)
  FileWordProvider(const char* path, size_t bufSize = 2048);
  ~FileWordProvider() override;
  bool isValid() const {
    return (bool)file_;
  }

  bool hasNextWord() override;
  String getNextWord(TextRenderer& renderer) override;
  String getPrevWord(TextRenderer& renderer) override;

  float getPercentage() override;
  float getPercentage(int index) override;
  void setPosition(int index) override;
  int getCurrentIndex() override;
  void ungetWord() override;
  void reset() override;

 private:
  String scanWord(int direction, TextRenderer& renderer);

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
