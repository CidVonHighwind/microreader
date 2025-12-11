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

  // Paragraph alignment support
  TextAlign getParagraphAlignment() override;

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
  TextAlign currentParagraphAlignment_ = TextAlign::Left;

  // Parse and skip an ESC style token starting at `pos` (forward direction).
  // Token format: ESC [ content ] ESC
  // Returns number of characters consumed by the token (0 if not a token).
  size_t parseEscTokenAtPos(size_t pos);

  // Find the start of an ESC token when positioned at its trailing ESC.
  // Returns the position of the leading ESC, or SIZE_MAX if not found.
  size_t findEscTokenStart(size_t trailingEscPos);

  // Check if position is inside an ESC token (not at boundaries).
  // If so, returns the token length; otherwise returns 0.
  bool isEscChar(char c) {
    return c == (char)27;
  }
};

#endif
