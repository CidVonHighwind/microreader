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

  if ((direction == 1 && index_ >= fileSize_) || (direction == -1 && index_ == 0 && fileSize_ == 0)) {
    return String("");
  }

  long currentPos = (direction == 1) ? (long)index_ : (long)index_ - 1;
  if ((direction == 1 && currentPos >= (long)fileSize_) || (direction == -1 && currentPos < 0)) {
    return String("");
  }

  char c = charAt((size_t)currentPos);

  if (c == ' ') {
    String token;
    long start = currentPos;
    if (direction == 1) {
      long end = currentPos;
      while (end < (long)fileSize_ && charAt((size_t)end) == ' ')
        end++;
      // build token from start..end
      for (long i = start; i < end; ++i)
        token += charAt((size_t)i);
      index_ = (size_t)end;
    } else {
      while (start > 0 && charAt((size_t)(start - 1)) == ' ')
        start--;
      for (long i = start; i < (long)index_; ++i)
        token += charAt((size_t)i);
      index_ = (size_t)start;
    }
    return token;
  } else if (c == '\r') {
    if (direction == 1) {
      index_++;
    } else {
      index_ = (size_t)currentPos;
    }
    // Ignore carriage return
    return scanWord(direction);
  } else if (c == '\n' || c == '\t') {
    if (direction == 1) {
      index_++;
    } else {
      index_ = (size_t)currentPos;
    }
    String s;
    s += c;
    return s;
  } else {
    String token;
    long start = currentPos;
    if (direction == 1) {
      long end = currentPos;
      while (end < (long)fileSize_) {
        char cc = charAt((size_t)end);
        if (cc == ' ' || cc == '\n' || cc == '\t' || cc == '\r')
          break;
        end++;
      }
      for (long i = start; i < end; ++i)
        token += charAt((size_t)i);
      index_ = (size_t)end;
    } else {
      while (start > 0) {
        char cc = charAt((size_t)(start - 1));
        if (cc == ' ' || cc == '\n' || cc == '\t' || cc == '\r')
          break;
        start--;
      }
      for (long i = start; i < (long)index_; ++i)
        token += charAt((size_t)i);
      index_ = (size_t)start;
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
