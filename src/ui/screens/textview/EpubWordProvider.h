#ifndef EPUB_WORD_PROVIDER_H
#define EPUB_WORD_PROVIDER_H

#include <SD.h>

#include <cstdint>

#include "WordProvider.h"

class EpubWordProvider : public WordProvider {
 public:
  // path: SD path to epub file
  // bufSize: decompressed text buffer size (default 4096)
  EpubWordProvider(const char* path, size_t bufSize = 4096);
  ~EpubWordProvider() override;
  bool isValid() const {
    return valid_;
  }

  bool hasNextWord() override;
  String getNextWord() override;
  String getPrevWord() override;

  float getPercentage() override;
  float getPercentage(int index) override;
  void setPosition(int index) override;
  int getCurrentIndex() override;
  char peekChar(int offset = 0) override;
  bool isInsideWord() override;
  void ungetWord() override;
  void reset() override;

 private:
  bool valid_ = false;
  size_t index_ = 0;
  size_t prevIndex_ = 0;
  size_t bufSize_ = 0;

  String epubPath_;
};

#endif
