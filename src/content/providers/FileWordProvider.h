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
  StyledWord getNextWord() override;
  StyledWord getPrevWord() override;

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
  StyledWord scanWord(int direction);

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

  // Paragraph alignment caching
  TextAlign cachedParagraphAlignment_ = TextAlign::Left;
  size_t cachedParagraphStart_ = SIZE_MAX;  // SIZE_MAX = invalid/not cached
  size_t cachedParagraphEnd_ = SIZE_MAX;

  // Current inline font style (updated when parsing [style=...] tokens)
  FontStyle currentInlineStyle_ = FontStyle::REGULAR;

  // Find paragraph boundaries containing the given position
  void findParagraphBoundaries(size_t pos, size_t& outStart, size_t& outEnd);
  // Update the paragraph alignment cache for current position
  void updateParagraphAlignmentCache();

  // Parse and skip an ESC token starting at `pos` (forward direction).
  // ESC format: ESC + command byte (2 bytes total, fixed length)
  // Alignment: ESC+'L'(left), ESC+'R'(right), ESC+'C'(center), ESC+'J'(justify)
  // Style: ESC+'B'(bold), ESC+'b'(end bold), ESC+'I'(italic), ESC+'i'(end italic),
  //        ESC+'X'(bold+italic), ESC+'x'(end bold+italic)
  // Returns 2 if valid ESC token found, 0 otherwise.
  // If outAlignment is provided, writes the parsed alignment there.
  // If processStyle is false, only checks validity without modifying state.
  size_t parseEscTokenAtPos(size_t pos, TextAlign* outAlignment = nullptr, bool processStyle = true);

  // Check if there's a valid ESC token at pos (without modifying state)
  size_t checkEscTokenAtPos(size_t pos);

  // Parse ESC token when reading BACKWARD - style meanings are inverted
  // When going backward through "ESC+B text ESC+b", we encounter ESC+b first
  // (entering bold region) and ESC+B second (exiting bold region)
  void parseEscTokenBackward(size_t pos);

  // Restore style context after seeking to an arbitrary position.
  // Scans backward from current position to find the most recent style token.
  // Stops at paragraph boundary (newline) which resets style to REGULAR.
  void restoreStyleContext();

  // Find the start of an ESC token when positioned at its command byte.
  // Returns the position of the ESC character, or SIZE_MAX if not found.
  size_t findEscTokenStart(size_t commandBytePos);

  // Check if we're at the end of an ESC token (at command byte position).
  // Returns true and sets tokenStart if found.
  bool isAtEscTokenEnd(size_t pos, size_t& tokenStart);

  // Helper method to determine if a character is a word boundary
  bool isWordBoundary(char c);
};

#endif
