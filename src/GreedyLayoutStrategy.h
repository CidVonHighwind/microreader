#ifndef GREEDY_LAYOUT_STRATEGY_H
#define GREEDY_LAYOUT_STRATEGY_H

#include "LayoutStrategy.h"

class GreedyLayoutStrategy : public LayoutStrategy {
 public:
  GreedyLayoutStrategy();
  ~GreedyLayoutStrategy();

  Type getType() const override {
    return GREEDY;
  }

  // Main interface implementation
  void layoutText(const String& text, TextRenderer& renderer, const LayoutConfig& config) override;

 private:
  float spaceWidth_;

  // Helper methods
  std::vector<Word> tokenizeAndMeasure(const String& paragraph, TextRenderer& renderer);
  int16_t layoutAndRender(const std::vector<Word>& words, TextRenderer& renderer, int16_t x, int16_t y,
                          int16_t maxWidth, int16_t lineHeight, int16_t maxY);
  std::vector<size_t> calculateBreaks(const std::vector<Word>& words, float maxWidth);
};

#endif
