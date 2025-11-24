#ifndef WORD_PROVIDER_H
#define WORD_PROVIDER_H

#include "LayoutStrategy.h"  // For the Word struct

class TextRenderer;  // Forward declaration

class WordProvider {
 public:
  virtual ~WordProvider() = default;

  // Returns true if there are more words to read
  virtual bool hasNextWord() = 0;

  // Returns the next word, measuring its width using the renderer
  virtual LayoutStrategy::Word getNextWord(TextRenderer& renderer) = 0;

  // Gets the previous word and moves index backwards
  virtual LayoutStrategy::Word getPrevWord(TextRenderer& renderer) = 0;

  // Returns the current reading progress as a percentage (0.0 to 1.0)
  virtual float getPercentage() = 0;

  // Sets the reading position to the given index in the text
  virtual void setPosition(int index) = 0;

  // Returns the current index position in the text
  virtual int getCurrentIndex() = 0;

  // Puts back the last word retrieved by getNextWord (moves index back)
  virtual void ungetWord() = 0;

  // Resets the provider to the beginning (optional, for rewinding)
  virtual void reset() = 0;
};

#endif