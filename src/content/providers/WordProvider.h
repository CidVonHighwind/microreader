#ifndef WORD_PROVIDER_H
#define WORD_PROVIDER_H

#include "../css/CssStyle.h"  // For TextAlign and CssStyle
#include "WString.h"          // For Arduino `String`

class TextRenderer;  // Forward declaration

class WordProvider {
 public:
  virtual ~WordProvider() = default;

  // Returns true if there are more words to read forward
  virtual bool hasNextWord() = 0;

  // Returns true if there are more words to read backward
  virtual bool hasPrevWord() = 0;

  // Returns the next word as an Arduino `String`, measuring its width using the renderer
  virtual String getNextWord() = 0;

  // Gets the previous word as an Arduino `String` and moves index backwards
  virtual String getPrevWord() = 0;

  // Returns the current reading progress as a percentage (0.0 to 1.0)
  virtual float getPercentage() = 0;
  virtual float getPercentage(int index) = 0;

  // Returns the current chapter progress as a percentage (0.0 to 1.0)
  // For single-file providers, this is the same as getPercentage()
  virtual float getChapterPercentage() {
    return getPercentage();
  }
  virtual float getChapterPercentage(int index) {
    return getPercentage(index);
  }

  // Sets the reading position to the given index in the text
  virtual void setPosition(int index) = 0;

  // Returns the current index position in the text
  virtual int getCurrentIndex() = 0;

  // Peek at a character at the current index + offset (without changing position)
  // Returns '\0' if position is out of bounds
  virtual char peekChar(int offset = 0) = 0;

  // Consume n text characters, advancing through inline elements as needed
  // This is useful for positioning after a hyphen split
  // Returns the number of characters actually consumed
  virtual int consumeChars(int n) = 0;

  // Check if current position is inside a word (surrounded by non-whitespace characters)
  // Returns true if both previous and current characters are word characters
  virtual bool isInsideWord() = 0;

  // Puts back the last word retrieved by getNextWord (moves index back)
  virtual void ungetWord() = 0;

  // Resets the provider to the beginning (optional, for rewinding)
  virtual void reset() = 0;

  // Chapter navigation (for providers that support multiple chapters like EPUB)
  // Returns the number of chapters/sections (1 for single-file providers)
  virtual int getChapterCount() {
    return 1;
  }

  // Returns the current chapter index (0-based)
  virtual int getCurrentChapter() {
    return 0;
  }

  // Sets the current chapter and optionally resets position to start of chapter
  // Returns true if successful, false if chapter index is out of range
  virtual bool setChapter(int chapterIndex) {
    return chapterIndex == 0;
  }

  // Returns true if the provider supports multiple chapters
  virtual bool hasChapters() {
    return false;
  }

  // Returns the name/title of the current chapter (empty string if not available)
  virtual String getCurrentChapterName() {
    return String("");
  }

  // Style support - returns the currently active style for styling words
  // The default implementation returns a default style (left-aligned)
  virtual CssStyle getCurrentStyle() {
    return CssStyle();
  }

  // Current paragraph alignment - default left
  virtual TextAlign getParagraphAlignment() {
    return TextAlign::Left;
  }
};

#endif