#include "FileWordProvider.h"

#include <Arduino.h>

#include "WString.h"

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

String FileWordProvider::scanWord(int direction) {
  prevIndex_ = index_;

  if ((direction == 1 && index_ >= fileSize_) || (direction == -1 && index_ == 0)) {
    return String("");
  }

  // Helper: skip any ESC token at current position (forward)
  auto skipTokenForward = [this](long& pos) {
    while (pos < (long)fileSize_) {
      size_t tokenLen = parseEscTokenAtPos((size_t)pos);
      if (tokenLen == 0)
        break;
      pos += (long)tokenLen;
    }
  };

  // Helper: skip any ESC token ending at current position (backward)
  // When scanning backward, if we see a trailing ESC, find the token start
  auto skipTokenBackward = [this](long& pos) {
    while (pos > 0) {
      char c = charAt((size_t)(pos - 1));
      if (!isEscChar(c))
        break;
      // Could be trailing ESC - look for ] before it
      if (pos >= 2 && charAt((size_t)(pos - 2)) == ']') {
        size_t tokenStart = findEscTokenStart((size_t)(pos - 1));
        if (tokenStart != SIZE_MAX) {
          // Parse the token to update alignment state
          parseEscTokenAtPos(tokenStart);
          pos = (long)tokenStart;
          continue;
        }
      }
      // Could be leading ESC - check if this starts a valid token
      size_t tokenLen = parseEscTokenAtPos((size_t)(pos - 1));
      if (tokenLen > 0) {
        pos = pos - 1;
        continue;
      }
      break;
    }
  };

  // Helper: check if char is a word boundary
  auto isWordBoundary = [](char c) { return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\0'; };

  long currentPos = (direction == 1) ? (long)index_ : (long)index_ - 1;

  // Skip any tokens at starting position
  if (direction == 1) {
    skipTokenForward(currentPos);
    if (currentPos >= (long)fileSize_)
      return String("");
  } else {
    // For backward, adjust position first then skip tokens
    currentPos = (long)index_;
    skipTokenBackward(currentPos);
    if (currentPos <= 0)
      return String("");
    currentPos--;  // Look at char before position
  }

  char c = charAt((size_t)currentPos);

  if (c == ' ') {
    // Space token
    long start = currentPos;
    long end = currentPos;

    if (direction == 1) {
      // Scan forward through spaces
      while (end < (long)fileSize_) {
        skipTokenForward(end);
        if (end >= (long)fileSize_)
          break;
        if (charAt((size_t)end) == ' ')
          end++;
        else
          break;
      }
      index_ = (size_t)end;
    } else {
      // Scan backward through spaces
      start = currentPos + 1;  // start after the space we're at
      while (start > 0) {
        skipTokenBackward(start);
        if (start <= 0)
          break;
        if (charAt((size_t)(start - 1)) == ' ')
          start--;
        else
          break;
      }
      index_ = (size_t)start;
      // Rebuild token from start to original position + 1
      end = currentPos + 1;
    }

    // Build the space token (just spaces, no ESC content)
    String token;
    for (long i = (direction == 1 ? currentPos : start); i < (direction == 1 ? end : currentPos + 1);) {
      size_t escLen = parseEscTokenAtPos((size_t)i);
      if (escLen > 0) {
        i += (long)escLen;
        continue;
      }
      token += charAt((size_t)i);
      i++;
    }
    return token;

  } else if (c == '\r') {
    // Skip carriage return
    if (direction == 1) {
      index_ = (size_t)(currentPos + 1);
    } else {
      index_ = (size_t)currentPos;
    }
    return scanWord(direction);

  } else if (c == '\n' || c == '\t') {
    // Single newline or tab
    if (direction == 1) {
      index_ = (size_t)(currentPos + 1);
    } else {
      index_ = (size_t)currentPos;
    }
    String s;
    s += c;
    return s;

  } else {
    // Word token
    long start = currentPos;
    long end = currentPos;

    if (direction == 1) {
      // Scan forward to find word end
      while (end < (long)fileSize_) {
        skipTokenForward(end);
        if (end >= (long)fileSize_)
          break;
        char cc = charAt((size_t)end);
        if (isWordBoundary(cc))
          break;
        end++;
      }
      index_ = (size_t)end;
    } else {
      // Scan backward to find word start
      start = currentPos + 1;
      while (start > 0) {
        skipTokenBackward(start);
        if (start <= 0)
          break;
        char cc = charAt((size_t)(start - 1));
        if (isWordBoundary(cc))
          break;
        start--;
      }
      index_ = (size_t)start;
      end = currentPos + 1;
    }

    // Build the word (skip ESC tokens)
    String token;
    for (long i = (direction == 1 ? currentPos : start); i < (direction == 1 ? end : currentPos + 1);) {
      size_t escLen = parseEscTokenAtPos((size_t)i);
      if (escLen > 0) {
        i += (long)escLen;
        continue;
      }
      token += charAt((size_t)i);
      i++;
    }
    return token;
  }
}

String FileWordProvider::getNextWord() {
  return scanWord(+1);
}
String FileWordProvider::getPrevWord() {
  return scanWord(-1);
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
}

void FileWordProvider::reset() {
  index_ = 0;
  prevIndex_ = 0;
}

TextAlign FileWordProvider::getParagraphAlignment() {
  return currentParagraphAlignment_;
}

size_t FileWordProvider::findEscTokenStart(size_t trailingEscPos) {
  // Token format: ESC [ content ] ESC
  // We're at the trailing ESC, need to find the leading ESC
  // The character before trailing ESC should be ']'
  if (trailingEscPos < 3)
    return SIZE_MAX;  // Minimum token: ESC [ ] ESC = 4 chars
  if (charAt(trailingEscPos - 1) != ']')
    return SIZE_MAX;

  // Search backwards for ESC [
  const size_t MAX_SEARCH = 256;
  size_t minPos = (trailingEscPos > MAX_SEARCH) ? (trailingEscPos - MAX_SEARCH) : 0;

  for (size_t i = trailingEscPos - 2; i >= minPos; i--) {
    if (charAt(i) == '[' && i > 0 && isEscChar(charAt(i - 1))) {
      return i - 1;  // Return position of leading ESC
    }
    // Stop if we hit another ] (nested tokens not supported)
    if (charAt(i) == ']')
      return SIZE_MAX;
    if (i == 0)
      break;
  }
  return SIZE_MAX;
}

size_t FileWordProvider::parseEscTokenAtPos(size_t pos) {
  if (pos >= fileSize_)
    return 0;
  if (charAt(pos) != (char)27)  // ESC
    return 0;
  // '[' must follow
  if (charAt(pos + 1) != '[')
    return 0;

  // Find closing bracket
  size_t i = pos + 2;
  const size_t ESC_TOKEN_MAX_LEN = 256;
  size_t last = (pos + ESC_TOKEN_MAX_LEN < fileSize_) ? (pos + ESC_TOKEN_MAX_LEN) : fileSize_;
  size_t closePos = SIZE_MAX;
  for (; i < last; ++i) {
    if (charAt(i) == ']') {
      closePos = i;
      break;
    }
  }
  if (closePos == SIZE_MAX)
    return 0;
  // The token must be followed by a trailing ESC character to mark end of token
  if (closePos + 1 >= fileSize_)
    return 0;
  if (charAt(closePos + 1) != (char)27)
    return 0;

  // Extract content between '[' and ']' (exclusive)
  String content;
  for (size_t j = pos + 2; j < closePos; ++j) {
    char c = charAt(j);
    if (c == '\0')
      return 0;  // incomplete token
    content += c;
  }

  // Only support align=... tokens currently
  const String prefix = String("align=");
  if (content.indexOf(prefix.c_str(), 0) != 0)
    return 0;
  String val = content.substring((int)prefix.length());
  // Normalize to lower-case
  val.toLowerCase();
  if (val == "left")
    currentParagraphAlignment_ = TextAlign::Left;
  else if (val == "right")
    currentParagraphAlignment_ = TextAlign::Right;
  else if (val == "center")
    currentParagraphAlignment_ = TextAlign::Center;
  else if (val == "justify")
    currentParagraphAlignment_ = TextAlign::Justify;
  else
    currentParagraphAlignment_ = TextAlign::Left;

  // Consume token + optional following space
  size_t consumed = (closePos - pos + 1);
  // we just validated closing ESC exists at closePos+1; include it
  consumed += 1;  // trailing ESC
  // Optional single space after token
  if (closePos + 2 < fileSize_ && charAt(closePos + 2) == ' ')
    consumed++;
  return consumed;
}
