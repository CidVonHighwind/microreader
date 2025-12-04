#ifndef LAYOUT_STRATEGY_H
#define LAYOUT_STRATEGY_H

#include <Arduino.h>
#include <WString.h>

#include <cstdint>
#include <vector>

// Forward declarations
class TextRenderer;
class WordProvider;
class HyphenationStrategy;
enum class Language;

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
    int16_t x;
    int16_t y;
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
    Language language;  // Language for hyphenation
  };

  // Paragraph result: multiple lines of words and the provider end position for each line
  struct Paragraph {
    std::vector<std::vector<Word>> lines;
    std::vector<int> lineEndPositions;  // provider index after each line
  };

  struct PageLayout {
    std::vector<std::vector<Word>> lines;
    int endPosition;  // provider index at end of page
  };

  LayoutStrategy();
  virtual ~LayoutStrategy();
  virtual Type getType() const = 0;

  // Set the language for hyphenation (updates hyphenation strategy)
  void setLanguage(Language language);

  // Main layout method: takes words from a provider and computes layout
  // Returns page layout with lines and end position
  virtual PageLayout layoutText(WordProvider& provider, TextRenderer& renderer, const LayoutConfig& config) = 0;

  // Render a previously computed page layout
  virtual void renderPage(const PageLayout& layout, TextRenderer& renderer, const LayoutConfig& config) = 0;

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
  struct HyphenSplit {
    int position;        // Character position of the split
    bool isAlgorithmic;  // True if hyphen needs to be inserted, false if it exists in text
    bool found;          // True if a valid split was found
  };
  // Shared helpers used by multiple strategies
  std::vector<Word> getNextLine(WordProvider& provider, TextRenderer& renderer, int16_t maxWidth, bool& isParagraphEnd);
  std::vector<Word> getPrevLine(WordProvider& provider, TextRenderer& renderer, int16_t maxWidth, bool& isParagraphEnd);

  // Word splitting helpers
  HyphenSplit findBestHyphenSplitForward(const String& word, int16_t availableWidth, TextRenderer& renderer);
  HyphenSplit findBestHyphenSplitBackward(const String& word, int16_t availableWidth, TextRenderer& renderer);

  // Shared space width used by layout and navigation
  uint16_t spaceWidth_ = 0;

  // Hyphenation strategy for current language
  HyphenationStrategy* hyphenationStrategy_ = nullptr;
};

#endif