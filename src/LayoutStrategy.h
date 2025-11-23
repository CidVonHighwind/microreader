#ifndef LAYOUT_STRATEGY_H
#define LAYOUT_STRATEGY_H

#include <WString.h>

#include <vector>

// Forward declarations
class TextRenderer;

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

  virtual ~LayoutStrategy() = default;
  virtual Type getType() const = 0;

  // Main layout method: takes full text and renders it
  virtual void layoutText(const String& text, TextRenderer& renderer, const LayoutConfig& config) = 0;

  // Optional lower-level methods for strategies that need them
  virtual void setSpaceWidth(float spaceWidth) {}
  virtual std::vector<Word> tokenizeAndMeasure(const String& paragraph, TextRenderer& renderer) {
    return std::vector<Word>();
  }
  virtual int16_t layoutAndRender(const std::vector<Word>& words, TextRenderer& renderer, int16_t x, int16_t y,
                                  int16_t maxWidth, int16_t lineHeight, int16_t maxY) {
    return y;
  }
};

#endif