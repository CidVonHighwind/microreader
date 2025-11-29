#ifndef KNUTH_PLASS_LAYOUT_STRATEGY_H
#define KNUTH_PLASS_LAYOUT_STRATEGY_H

#include <vector>

#include "LayoutStrategy.h"

class KnuthPlassLayoutStrategy : public LayoutStrategy {
 public:
  KnuthPlassLayoutStrategy();
  ~KnuthPlassLayoutStrategy();

  Type getType() const override {
    return KNUTH_PLASS;
  }

  // Main interface implementation
  int layoutText(WordProvider& provider, TextRenderer& renderer, const LayoutConfig& config) override;

 private:
  // spaceWidth_ is defined in base class

  // Knuth-Plass parameters
  static constexpr float INFINITY_PENALTY = 10000.0f;
  static constexpr float HYPHEN_PENALTY = 50.0f;
  static constexpr float FITNESS_DEMERITS = 100.0f;

  // Node for dynamic programming
  struct Node {
    size_t position;      // Word index
    size_t line;          // Line number
    float totalDemerits;  // Total demerits up to this point
    int16_t totalWidth;   // Width accumulated up to this position
    int prevBreak;        // Previous break point index (-1 if none)
  };

  // Helper methods
  int16_t layoutAndRender(const std::vector<Word>& words, TextRenderer& renderer, int16_t x, int16_t y,
                          int16_t maxWidth, int16_t lineHeight, int16_t maxY, TextAlignment alignment,
                          bool paragraphEnd = true);
  std::vector<size_t> calculateBreaks(const std::vector<Word>& words, int16_t maxWidth);
  float calculateBadness(int16_t actualWidth, int16_t targetWidth);
  float calculateDemerits(float badness, bool isLastLine);
};

#endif
