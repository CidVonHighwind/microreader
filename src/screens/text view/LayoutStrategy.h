#ifndef LAYOUT_STRATEGY_H
#define LAYOUT_STRATEGY_H

#include <Arduino.h>
#include <WString.h>

#include <cstdint>
#include <vector>

// Forward declarations
class TextRenderer;
class WordProvider;

/**
 * Abstract base class for line breaking strategies.
 * Implementations of this interface define different algorithms
 * for breaking text into lines (e.g., greedy, optimal, balanced).
 */
class LayoutStrategy {
 public:
  enum Type { GREEDY, KNUTH_PLASS };

  enum TextAlignment { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

  struct Word {
    String text;
    int16_t width;
  };

  struct LayoutConfig {
    int16_t marginLeft;
    int16_t marginRight;
    int16_t marginTop;
    int16_t marginBottom;
    int16_t lineHeight;
    int16_t minSpaceWidth;
    int16_t pageWidth;
    int16_t pageHeight;
    TextAlignment alignment;
  };

  // Paragraph result: multiple lines of words and the provider end position for each line
  struct Paragraph {
    std::vector<std::vector<Word>> lines;
    std::vector<int> lineEndPositions;  // provider index after each line
  };

  virtual ~LayoutStrategy() = default;
  virtual Type getType() const = 0;

  // Main layout method: takes words from a provider and renders them
  // Returns the provider character index at the end of the page (end position)
  virtual int layoutText(WordProvider& provider, TextRenderer& renderer, const LayoutConfig& config) = 0;

  // Calculate the start position of the previous page given current position
  // Calculate the start position of the previous page. A default implementation is
  // provided in the base class using provider backward scanning; derived classes
  // may optionally override if they want a different behavior.
  virtual int getPreviousPageStart(WordProvider& provider, TextRenderer& renderer, const LayoutConfig& config,
                                   int currentEndPosition);

  // Optional lower-level methods for strategies that need them
  virtual void setSpaceWidth(float spaceWidth) {
    spaceWidth_ = static_cast<int16_t>(spaceWidth);
  }
  virtual int16_t layoutAndRender(const std::vector<Word>& words, TextRenderer& renderer, int16_t x, int16_t y,
                                  int16_t maxWidth, int16_t lineHeight, int16_t maxY) {
    return y;
  }

  // Test wrappers for common navigation helpers. Tests should use these instead
  // of dependent-strategy-specific functions.
  std::vector<Word> test_getPrevLine(WordProvider& provider, TextRenderer& renderer, int16_t maxWidth,
                                     bool& isParagraphEnd);
  int test_getPreviousPageStart(WordProvider& provider, TextRenderer& renderer, const LayoutConfig& config,
                                int currentStartPosition);
  std::vector<Word> test_getNextLineDefault(WordProvider& provider, TextRenderer& renderer, int16_t maxWidth,
                                            bool& isParagraphEnd);

 protected:
  // Shared helpers used by multiple strategies
  std::vector<Word> getNextLine(WordProvider& provider, TextRenderer& renderer, int16_t maxWidth, bool& isParagraphEnd);
  std::vector<Word> getPrevLine(WordProvider& provider, TextRenderer& renderer, int16_t maxWidth, bool& isParagraphEnd);

  // Shared space width used by layout and navigation
  uint16_t spaceWidth_ = 0;
};

#endif