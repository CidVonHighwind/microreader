#include "StringWordProvider.h"

// NOTE: To keep this class testable without pulling in platform-specific
// graphics/Arduino headers, do not depend on TextRenderer methods here.
// Width measurement is simplified to the character count of the word so
// unit tests can instantiate and exercise this class without stubbing
// the entire graphics stack.

StringWordProvider::StringWordProvider(const String& text) : text_(text), index_(0), prevIndex_(0) {}

StringWordProvider::~StringWordProvider() {}

bool StringWordProvider::hasNextWord() {
  return index_ < text_.length();
}

String StringWordProvider::getNextWord(TextRenderer& renderer) {
  return scanWord(+1, renderer);
}

String StringWordProvider::getPrevWord(TextRenderer& renderer) {
  return scanWord(-1, renderer);
}

String StringWordProvider::scanWord(int direction, TextRenderer& /*renderer*/) {
  // Save prevIndex_ when scanning
  prevIndex_ = index_;

  int currentPos = (direction == 1) ? index_ : index_ - 1;
  if ((direction == 1 && currentPos >= text_.length()) || (direction == -1 && currentPos < 0)) {
    return String("");
  }
  char c = text_[currentPos];

  if (c == ' ') {
    String token;
    int start = currentPos;
    if (direction == 1) {
      int end = currentPos;
      while (end < text_.length() && text_[end] == ' ')
        end++;
      token = text_.substring(start, end);
      index_ = end;
    } else {
      while (start > 0 && text_[start - 1] == ' ')
        start--;
      token = text_.substring(start, index_);
      index_ = start;
    }
    return token;
  } else if (c == '\n' || c == '\t' || c == '\r') {
    if (direction == 1) {
      index_++;
    } else {
      index_ = currentPos;
    }
    return String(c);
  } else {
    String token;
    int start = currentPos;
    if (direction == 1) {
      int end = currentPos;
      while (end < text_.length() && text_[end] != ' ' && text_[end] != '\n' && text_[end] != '\t' &&
             text_[end] != '\r')
        end++;
      token = text_.substring(start, end);
      index_ = end;
    } else {
      while (start > 0 && text_[start - 1] != ' ' && text_[start - 1] != '\n' && text_[start - 1] != '\t' &&
             text_[start - 1] != '\r')
        start--;
      token = text_.substring(start, index_);
      index_ = start;
    }
    return token;
  }
}

float StringWordProvider::getPercentage() {
  if (text_.length() == 0)
    return 1.0f;
  return static_cast<float>(index_) / static_cast<float>(text_.length());
}

float StringWordProvider::getPercentage(int index) {
  if (text_.length() == 0)
    return 1.0f;
  return static_cast<float>(index) / static_cast<float>(text_.length());
}

int StringWordProvider::getCurrentIndex() {
  return index_;
}

void StringWordProvider::ungetWord() {
  index_ = prevIndex_;
}

void StringWordProvider::setPosition(int index) {
  if (index < 0)
    index = 0;
  if (index > text_.length())
    index = text_.length();
  index_ = index;
}

void StringWordProvider::reset() {
  index_ = 0;
  prevIndex_ = 0;
}