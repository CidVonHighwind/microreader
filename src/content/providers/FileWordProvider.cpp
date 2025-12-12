#include "FileWordProvider.h"

#include <Arduino.h>

#include "WString.h"

// ESC-based format constants:
// Format: ESC + command byte (2 bytes total, fixed length)
// Alignment commands (start of line): ESC + 'L'(left), 'R'(right), 'C'(center), 'J'(justify)
// Style commands (inline): ESC + 'B'(bold on), 'b'(bold off), 'I'(italic on), 'i'(italic off),
//                          'X'(bold+italic on), 'x'(bold+italic off)
static constexpr char ESC_CHAR = '\x1B';

FileWordProvider::FileWordProvider(const char* path, size_t bufSize) : bufSize_(bufSize) {
  file_ = SD.open(path);
  if (!file_) {
    fileSize_ = 0;
    buf_ = nullptr;
    return;
  }
  fileSize_ = file_.size();
  index_ = 0;
  prevIndex_ = 0;
  buf_ = (uint8_t*)malloc(bufSize_);
  bufStart_ = 0;
  bufLen_ = 0;

  // Skip UTF-8 BOM if present
  if (fileSize_ >= 3) {
    if (ensureBufferForPos(0) && buf_[0] == 0xEF && buf_[1] == 0xBB && buf_[2] == 0xBF) {
      index_ = 3;  // Skip the 3-byte BOM
    }
  }
}

FileWordProvider::~FileWordProvider() {
  if (file_)
    file_.close();
  if (buf_)
    free(buf_);
}

bool FileWordProvider::hasNextWord() {
  return index_ < fileSize_;
}

bool FileWordProvider::hasPrevWord() {
  return index_ > 0;
}

char FileWordProvider::charAt(size_t pos) {
  if (pos >= fileSize_)
    return '\0';
  if (!ensureBufferForPos(pos))
    return '\0';
  return (char)buf_[pos - bufStart_];
}

bool FileWordProvider::ensureBufferForPos(size_t pos) {
  if (!file_ || !buf_)
    return false;
  if (pos >= bufStart_ && pos < bufStart_ + bufLen_)
    return true;

  // Center buffer around pos when possible
  size_t start = (pos > bufSize_ / 2) ? (pos - bufSize_ / 2) : 0;
  if (start + bufSize_ > fileSize_) {
    if (fileSize_ > bufSize_)
      start = fileSize_ - bufSize_;
    else
      start = 0;
  }

  if (!file_.seek(start))
    return false;
  size_t r = file_.read(buf_, bufSize_);
  if (r == 0)
    return false;
  bufStart_ = start;
  bufLen_ = r;
  return true;
}

// Check if position has an ESC token (ESC + command byte = 2 bytes)
// Returns 2 if valid ESC token, 0 otherwise
// If processStyle is false, only checks validity without modifying state.
size_t FileWordProvider::parseEscTokenAtPos(size_t pos, TextAlign* outAlignment, bool processStyle) {
  if (pos + 1 >= fileSize_)
    return 0;

  char c = charAt(pos);
  if (c != ESC_CHAR)
    return 0;

  char cmd = charAt(pos + 1);

  // Alignment commands
  switch (cmd) {
    case 'L':
      if (outAlignment)
        *outAlignment = TextAlign::Left;
      else if (processStyle)
        cachedParagraphAlignment_ = TextAlign::Left;
      return 2;
    case 'R':
      if (outAlignment)
        *outAlignment = TextAlign::Right;
      else if (processStyle)
        cachedParagraphAlignment_ = TextAlign::Right;
      return 2;
    case 'C':
      if (outAlignment)
        *outAlignment = TextAlign::Center;
      else if (processStyle)
        cachedParagraphAlignment_ = TextAlign::Center;
      return 2;
    case 'J':
      if (outAlignment)
        *outAlignment = TextAlign::Justify;
      else if (processStyle)
        cachedParagraphAlignment_ = TextAlign::Justify;
      return 2;

    // Style commands
    case 'B':
      if (processStyle)
        currentInlineStyle_ = FontStyle::BOLD;
      return 2;
    case 'b':
      if (processStyle)
        currentInlineStyle_ = FontStyle::REGULAR;
      return 2;
    case 'I':
      if (processStyle)
        currentInlineStyle_ = FontStyle::ITALIC;
      return 2;
    case 'i':
      if (processStyle)
        currentInlineStyle_ = FontStyle::REGULAR;
      return 2;
    case 'X':
      if (processStyle)
        currentInlineStyle_ = FontStyle::BOLD_ITALIC;
      return 2;
    case 'x':
      if (processStyle)
        currentInlineStyle_ = FontStyle::REGULAR;
      return 2;
  }

  return 0;  // Unknown command
}

// Check if there's a valid ESC token at pos (without modifying state)
size_t FileWordProvider::checkEscTokenAtPos(size_t pos) {
  return parseEscTokenAtPos(pos, nullptr, false);
}

// Parse ESC token when reading BACKWARD - style meanings are inverted
// When going backward through "ESC+B text ESC+b", we encounter ESC+b first (entering bold region)
// and ESC+B second (exiting bold region), so meanings must be swapped
void FileWordProvider::parseEscTokenBackward(size_t pos) {
  if (pos + 1 >= fileSize_)
    return;

  char c = charAt(pos);
  if (c != ESC_CHAR)
    return;

  char cmd = charAt(pos + 1);

  // Alignment commands - same meaning regardless of direction
  switch (cmd) {
    case 'L':
      cachedParagraphAlignment_ = TextAlign::Left;
      return;
    case 'R':
      cachedParagraphAlignment_ = TextAlign::Right;
      return;
    case 'C':
      cachedParagraphAlignment_ = TextAlign::Center;
      return;
    case 'J':
      cachedParagraphAlignment_ = TextAlign::Justify;
      return;

    // Style commands - INVERTED for backward reading
    // 'B' (start bold forward) = end bold backward
    case 'B':
      currentInlineStyle_ = FontStyle::REGULAR;
      return;
    // 'b' (end bold forward) = start bold backward
    case 'b':
      currentInlineStyle_ = FontStyle::BOLD;
      return;
    case 'I':
      currentInlineStyle_ = FontStyle::REGULAR;
      return;
    case 'i':
      currentInlineStyle_ = FontStyle::ITALIC;
      return;
    case 'X':
      currentInlineStyle_ = FontStyle::REGULAR;
      return;
    case 'x':
      currentInlineStyle_ = FontStyle::BOLD_ITALIC;
      return;
  }
}

// Check if we're at the end of an ESC token (at the command byte position)
// Returns true and sets tokenStart if found
bool FileWordProvider::isAtEscTokenEnd(size_t pos, size_t& tokenStart) {
  if (pos == 0)
    return false;

  // Check if previous char is ESC
  char prevChar = charAt(pos - 1);
  if (prevChar != ESC_CHAR)
    return false;

  // Verify this is a valid command byte
  char cmd = charAt(pos);
  if (cmd == 'L' || cmd == 'R' || cmd == 'C' || cmd == 'J' || cmd == 'B' || cmd == 'b' || cmd == 'I' || cmd == 'i' ||
      cmd == 'X' || cmd == 'x') {
    tokenStart = pos - 1;
    return true;
  }

  return false;
}

StyledWord FileWordProvider::getNextWord() {
  prevIndex_ = index_;

  if (index_ >= fileSize_) {
    return StyledWord();
  }

  // Skip any ESC tokens at current position first
  while (index_ < fileSize_) {
    size_t tokenLen = parseEscTokenAtPos(index_);
    if (tokenLen == 0)
      break;
    index_ += tokenLen;
  }

  if (index_ >= fileSize_) {
    return StyledWord();
  }

  // Skip carriage returns
  while (index_ < fileSize_ && charAt(index_) == '\r') {
    index_++;
  }

  if (index_ >= fileSize_) {
    return StyledWord();
  }

  // Capture style BEFORE reading the word content
  // This ensures the word gets the style that was active at its start,
  // not any style that might be set by ESC tokens encountered during word building
  FontStyle styleForWord = currentInlineStyle_;

  char c = charAt(index_);
  String token;

  // Case 1: Space - read just the space and stop
  if (c == ' ') {
    token += c;
    index_++;
  }
  // Case 2: Single character tokens (newline, tab) - read just that character
  else if (c == '\n' || c == '\t') {
    token += c;
    index_++;
  }
  // Case 3: Regular character - continue until boundary
  else {
    while (index_ < fileSize_) {
      // Check for ESC token - use checkEscTokenAtPos to detect without modifying state
      size_t tokenLen = checkEscTokenAtPos(index_);
      if (tokenLen > 0) {
        // ESC token marks word boundary - stop here without processing the token
        // The token will be processed on the next getNextWord() call
        break;
      }

      char cc = charAt(index_);
      // Skip carriage returns
      if (cc == '\r') {
        index_++;
        continue;
      }
      // Stop at space or whitespace boundaries
      if (cc == ' ' || cc == '\n' || cc == '\t') {
        break;
      }
      token += cc;
      index_++;
    }
  }

  return StyledWord(token, styleForWord);
}

StyledWord FileWordProvider::getPrevWord() {
  prevIndex_ = index_;

  if (index_ == 0) {
    return StyledWord();
  }

  // Move to just before current position
  index_--;

  // Skip backward over ESC tokens (fixed 2-byte format makes this simple)
  // Don't try to invert token meanings - just skip over them
  while (true) {
    // Check if we're at command byte of an ESC token
    if (index_ > 0) {
      size_t tokenStart;
      if (isAtEscTokenEnd(index_, tokenStart)) {
        // We're at command byte, skip back over the whole token
        if (tokenStart == 0) {
          // ESC token starts at position 0, nothing before it
          index_ = 0;
          return StyledWord();
        }
        index_ = tokenStart - 1;
        continue;
      }
    }

    // Check if we landed on ESC char itself (start of a token)
    if (charAt(index_) == ESC_CHAR) {
      // Check if valid token without modifying state
      size_t tokenLen = checkEscTokenAtPos(index_);
      if (tokenLen > 0) {
        if (index_ == 0) {
          // At start of file, nothing before this token
          return StyledWord();
        }
        index_--;
        continue;
      }
    }

    break;
  }

  // Skip backward over carriage returns
  while (index_ > 0 && charAt(index_) == '\r') {
    index_--;
  }

  // If we ended up at position 0 with an ESC char, we're at start of file with only tokens
  if (charAt(index_) == ESC_CHAR) {
    size_t tokenLen = checkEscTokenAtPos(index_);
    if (tokenLen > 0) {
      // Process the token backward before returning
      parseEscTokenBackward(index_);
      index_ = 0;
      return StyledWord();
    }
  }

  if (index_ >= fileSize_) {
    index_ = 0;
    return StyledWord();
  }

  char c = charAt(index_);
  String token;
  size_t tokenStart = index_;

  // Case 1: Space
  if (c == ' ') {
    token += c;
  }
  // Case 2: Single character tokens
  else if (c == '\n' || c == '\t') {
    token += c;
  }
  // Case 3: Regular word - find start
  else {
    // Find word start by scanning backward
    while (tokenStart > 0) {
      char prevChar = charAt(tokenStart - 1);
      // Stop at whitespace
      if (prevChar == ' ' || prevChar == '\n' || prevChar == '\t' || prevChar == '\r') {
        break;
      }
      // Stop at ESC token boundary - check if prev char is a command byte with ESC before it
      if (tokenStart >= 2) {
        size_t possibleTokenStart;
        if (isAtEscTokenEnd(tokenStart - 1, possibleTokenStart)) {
          break;
        }
      }
      // Stop if prev char is ESC (we're right after an ESC token)
      if (prevChar == ESC_CHAR) {
        break;
      }
      tokenStart--;
    }

    // Build word from start to current position
    for (size_t i = tokenStart; i <= index_; i++) {
      char cc = charAt(i);
      if (cc != '\r') {
        token += cc;
      }
    }

    index_ = tokenStart;
  }

  // Use restoreStyleContext to correctly compute the style at the word's START position
  // This handles complex style transitions (e.g., B->X->I) correctly by scanning
  // forward from paragraph start to index_ (which is now the word start)
  restoreStyleContext();
  FontStyle styleForWord = currentInlineStyle_;

  return StyledWord(token, styleForWord);
}

StyledWord FileWordProvider::scanWord(int direction) {
  // Legacy function - redirect to new implementations
  if (direction == 1) {
    return getNextWord();
  } else {
    return getPrevWord();
  }
}

float FileWordProvider::getPercentage() {
  if (fileSize_ == 0)
    return 1.0f;
  return static_cast<float>(index_) / static_cast<float>(fileSize_);
}

float FileWordProvider::getPercentage(int index) {
  if (fileSize_ == 0)
    return 1.0f;
  return static_cast<float>(index) / static_cast<float>(fileSize_);
}

int FileWordProvider::getCurrentIndex() {
  return (int)index_;
}

char FileWordProvider::peekChar(int offset) {
  long pos = (long)index_ + offset;
  if (pos < 0 || pos >= (long)fileSize_) {
    return '\0';
  }
  return charAt((size_t)pos);
}

int FileWordProvider::consumeChars(int n) {
  if (n <= 0) {
    return 0;
  }

  int consumed = 0;
  while (consumed < n && index_ < fileSize_) {
    char c = charAt(index_);
    index_++;
    // Skip carriage returns, they don't count as consumed characters
    if (c != '\r') {
      consumed++;
    }
  }
  return consumed;
}

// Helper method to determine if a character is a word boundary
bool FileWordProvider::isWordBoundary(char c) {
  return (c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == ESC_CHAR);
}

bool FileWordProvider::isInsideWord() {
  if (index_ <= 0 || index_ >= fileSize_) {
    return false;
  }

  // Helper lambda to check if a character is a word character (not whitespace/control)
  auto isWordChar = [](char c) { return c != '\0' && c != ' ' && c != '\n' && c != '\t' && c != '\r'; };

  // Check character before current position
  char prevChar = charAt(index_ - 1);
  // Check character at current position
  char currentChar = charAt(index_);

  return isWordChar(prevChar) && isWordChar(currentChar);
}

void FileWordProvider::ungetWord() {
  index_ = prevIndex_;
}

void FileWordProvider::setPosition(int index) {
  if (index < 0)
    index = 0;
  if ((size_t)index > fileSize_)
    index = (int)fileSize_;
  index_ = (size_t)index;
  prevIndex_ = index_;
  // Restore style context for the new position
  restoreStyleContext();
  // Don't invalidate cache here - getParagraphAlignment will check if we're still in range
}

void FileWordProvider::reset() {
  index_ = 0;
  prevIndex_ = 0;
  // Invalidate paragraph alignment cache
  cachedParagraphStart_ = SIZE_MAX;
  cachedParagraphEnd_ = SIZE_MAX;
  cachedParagraphAlignment_ = TextAlign::Left;
}

TextAlign FileWordProvider::getParagraphAlignment() {
  // Check if current position is within cached paragraph range
  if (cachedParagraphStart_ != SIZE_MAX && index_ >= cachedParagraphStart_ && index_ < cachedParagraphEnd_) {
    return cachedParagraphAlignment_;
  }
  // Need to update cache for new paragraph
  updateParagraphAlignmentCache();
  return cachedParagraphAlignment_;
}

void FileWordProvider::findParagraphBoundaries(size_t pos, size_t& outStart, size_t& outEnd) {
  // Paragraphs are delimited by newlines
  // Find start: scan backwards to find newline or beginning of file
  outStart = 0;
  if (pos > 0) {
    for (size_t i = pos; i > 0; --i) {
      if (charAt(i - 1) == '\n') {
        outStart = i;
        break;
      }
    }
  }

  // Find end: scan forwards to find newline or end of file
  outEnd = fileSize_;
  for (size_t i = pos; i < fileSize_; ++i) {
    if (charAt(i) == '\n') {
      outEnd = i + 1;  // Include the newline in this paragraph
      break;
    }
  }
}

void FileWordProvider::updateParagraphAlignmentCache() {
  // Find paragraph boundaries for current position
  size_t paraStart, paraEnd;
  findParagraphBoundaries(index_, paraStart, paraEnd);

  // Cache the boundaries
  cachedParagraphStart_ = paraStart;
  cachedParagraphEnd_ = paraEnd;

  // Default alignment
  cachedParagraphAlignment_ = TextAlign::Left;

  // Scan through all ESC tokens at start of paragraph to find alignment token
  // There may be style tokens (like ESC+b from previous paragraph) before the alignment token
  size_t scanPos = paraStart;
  while (scanPos < paraEnd) {
    if (charAt(scanPos) != ESC_CHAR) {
      break;  // No more ESC tokens
    }
    // Try to parse as alignment token
    TextAlign foundAlign;
    size_t tokenLen = parseEscTokenAtPos(scanPos, &foundAlign, false);  // Don't process style
    if (tokenLen == 0) {
      break;  // Not a valid ESC token
    }
    // Check if it was an alignment token (L, R, C, J)
    char cmd = charAt(scanPos + 1);
    if (cmd == 'L' || cmd == 'R' || cmd == 'C' || cmd == 'J') {
      cachedParagraphAlignment_ = foundAlign;
      break;  // Found alignment, stop scanning
    }
    // Skip this style token and continue looking for alignment
    scanPos += tokenLen;
  }
}

size_t FileWordProvider::findEscTokenStart(size_t trailingPos) {
  // For ESC format, token is always 2 bytes: ESC + command
  // If trailingPos is at command byte, start is trailingPos - 1
  if (trailingPos == 0)
    return SIZE_MAX;

  if (charAt(trailingPos - 1) == ESC_CHAR) {
    return trailingPos - 1;
  }
  return SIZE_MAX;
}

void FileWordProvider::restoreStyleContext() {
  // Reset style to default first
  currentInlineStyle_ = FontStyle::REGULAR;

  if (index_ == 0 || fileSize_ == 0)
    return;

  // Find paragraph start (newline boundary)
  size_t paraStart = 0;
  for (size_t i = index_; i > 0; --i) {
    if (charAt(i - 1) == '\n') {
      paraStart = i;
      break;
    }
  }

  // Scan forward from paragraph start to current position, processing style tokens
  // This gives us the correct style state at the current position
  size_t scanPos = paraStart;
  while (scanPos < index_) {
    // Check for ESC token
    if (charAt(scanPos) == ESC_CHAR && scanPos + 1 < fileSize_) {
      char cmd = charAt(scanPos + 1);
      // Only process style tokens (not alignment which is paragraph-level)
      switch (cmd) {
        case 'B':
          currentInlineStyle_ = FontStyle::BOLD;
          break;
        case 'b':
          currentInlineStyle_ = FontStyle::REGULAR;
          break;
        case 'I':
          currentInlineStyle_ = FontStyle::ITALIC;
          break;
        case 'i':
          currentInlineStyle_ = FontStyle::REGULAR;
          break;
        case 'X':
          currentInlineStyle_ = FontStyle::BOLD_ITALIC;
          break;
        case 'x':
          currentInlineStyle_ = FontStyle::REGULAR;
          break;
      }
      scanPos += 2;  // Skip ESC token
    } else {
      scanPos++;
    }
  }
}
