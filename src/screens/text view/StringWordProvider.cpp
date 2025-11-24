#include "StringWordProvider.h"

#include "TextRenderer.h"

StringWordProvider::StringWordProvider(const String& text) : text_(text), index_(0), prevIndex_(0) {}

StringWordProvider::~StringWordProvider() {}

bool StringWordProvider::hasNextWord() {
  // Skip whitespace to check if there's a word ahead
  int tempIndex = index_;
  while (tempIndex < text_.length() &&
         (text_[tempIndex] == ' ' || text_[tempIndex] == '\n' || text_[tempIndex] == '\t')) {
    tempIndex++;
  }
  return tempIndex < text_.length();
}

LayoutStrategy::Word StringWordProvider::getNextWord(TextRenderer& renderer) {
  prevIndex_ = index_;  // Save position before advancing

  // Skip leading whitespace and count consecutive newlines
  int consecutiveNewlines = 0;
  while (index_ < text_.length()) {
    char c = text_[index_];
    if (c == '\n') {
      consecutiveNewlines++;
    } else if (c == ' ' || c == '\t') {
      consecutiveNewlines = 0;  // Reset on space/tab
    } else {
      break;  // Found non-whitespace
    }
    index_++;
  }

  // Return break words for consecutive newlines
  if (consecutiveNewlines >= 2) {
    return {"\n\n", 0};  // Paragraph break
  } else if (consecutiveNewlines == 1) {
    return {"\n", 0};  // Line break
  }

  // Check if we've reached the end
  if (index_ >= text_.length()) {
    return {"", 0};
  }

  // Find the end of the word
  int wordStart = index_;
  while (index_ < text_.length() && text_[index_] != ' ' && text_[index_] != '\n' && text_[index_] != '\t') {
    index_++;
  }

  String wordText = text_.substring(wordStart, index_);

  // Measure width
  int16_t x1, y1;
  uint16_t w, h;
  renderer.getTextBounds(wordText.c_str(), 0, 0, &x1, &y1, &w, &h);
  int16_t actualWidth = x1 + w;

  return {wordText, actualWidth};
}

LayoutStrategy::Word StringWordProvider::getPrevWord(TextRenderer& renderer) {
  if (index_ == 0) {
    return {"", 0};
  }

  prevIndex_ = index_;  // Save position before moving backwards

  // Start from before current index
  int searchPos = index_ - 1;

  // Skip backwards past whitespace
  int consecutiveNewlines = 0;
  while (searchPos >= 0) {
    char c = text_[searchPos];
    if (c == '\n') {
      consecutiveNewlines++;
    } else if (c == ' ' || c == '\t') {
      consecutiveNewlines = 0;
    } else {
      break;  // Found non-whitespace
    }
    searchPos--;
  }

  // If we found consecutive newlines, return a break word
  if (consecutiveNewlines >= 2) {
    index_ = searchPos;
    return {"\n\n", 0};  // Paragraph break
  } else if (consecutiveNewlines == 1) {
    index_ = searchPos;
    return {"\n", 0};  // Line break
  }

  if (searchPos < 0) {
    index_ = 0;
    return {"", 0};
  }

  // searchPos is at the end of the previous word
  int wordEnd = searchPos;

  // Find word start
  while (searchPos >= 0 && text_[searchPos] != ' ' && text_[searchPos] != '\n' && text_[searchPos] != '\t') {
    searchPos--;
  }

  int wordStart = searchPos + 1;

  // Extract word
  String wordText = text_.substring(wordStart, wordEnd + 1);

  // Update index to word start
  index_ = wordStart;

  // Measure width
  int16_t x1, y1;
  uint16_t w, h;
  renderer.getTextBounds(wordText.c_str(), 0, 0, &x1, &y1, &w, &h);
  int16_t actualWidth = x1 + w;

  return {wordText, actualWidth};
}

float StringWordProvider::getPercentage() {
  if (text_.length() == 0)
    return 1.0f;
  return static_cast<float>(index_) / static_cast<float>(text_.length());
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