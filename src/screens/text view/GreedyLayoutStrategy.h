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
  int layoutText(WordProvider& provider, TextRenderer& renderer, const LayoutConfig& config) override;

  // Calculate the start position of the previous page
  int getPreviousPageStart(WordProvider& provider, TextRenderer& renderer, const LayoutConfig& config,
                           int currentEndPosition) override;

 private:
  uint16_t spaceWidth_;

  // Helper methods
  std::vector<LayoutStrategy::Word> getNextLine(WordProvider& provider, TextRenderer& renderer, int16_t maxWidth,
                                                bool& isParagraphBreak, bool& isLineBreak);
  std::vector<LayoutStrategy::Word> getPrevLine(WordProvider& provider, TextRenderer& renderer, int16_t maxWidth,
                                                bool& isParagraphBreak, bool& isLineBreak);
  int16_t renderLine(const std::vector<LayoutStrategy::Word>& line, TextRenderer& renderer, int16_t x, int16_t y,
                     int16_t maxWidth, int16_t lineHeight, TextAlignment alignment);
};

#endif
