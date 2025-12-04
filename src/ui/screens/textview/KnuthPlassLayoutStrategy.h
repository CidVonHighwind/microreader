#ifndef KNUTH_PLASS_LAYOUT_STRATEGY_H
#define KNUTH_PLASS_LAYOUT_STRATEGY_H

#include <vector>

#include "LayoutStrategy.h"

class KnuthPlassLayoutStrategy : public LayoutStrategy {
 public:
  KnuthPlassLayoutStrategy();
  ~KnuthPlassLayoutStrategy();

  // Test support: check if line count mismatch occurred
  bool hasLineCountMismatch() const {
    return lineCountMismatch_;
  }
  int getExpectedLineCount() const {
    return expectedLineCount_;
  }
  int getActualLineCount() const {
    return actualLineCount_;
  }
  void resetLineCountMismatch() {
    lineCountMismatch_ = false;
    expectedLineCount_ = 0;
    actualLineCount_ = 0;
  }

  Type getType() const override {
    return KNUTH_PLASS;
  }

  // Main interface implementation
  PageLayout layoutText(WordProvider& provider, TextRenderer& renderer, const LayoutConfig& config) override;
  void renderPage(const PageLayout& layout, TextRenderer& renderer, const LayoutConfig& config) override;

 private:
  // spaceWidth_ is defined in base class

  struct ParagraphLayoutInfo {
    std::vector<Word> words;
    std::vector<size_t> breaks;
    bool paragraphEnd;
    int16_t yStart;
  };

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
  std::vector<size_t> calculateBreaks(const std::vector<Word>& words, int16_t maxWidth);
  float calculateBadness(int16_t actualWidth, int16_t targetWidth);
  float calculateDemerits(float badness, bool isLastLine);

  // Line count mismatch tracking for testing
  bool lineCountMismatch_ = false;
  int expectedLineCount_ = 0;
  int actualLineCount_ = 0;
};

#endif
