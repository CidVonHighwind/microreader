#ifndef GREEDY_LAYOUT_STRATEGY_H
#define GREEDY_LAYOUT_STRATEGY_H

#include <cstdint>

#include "LayoutStrategy.h"

class GreedyLayoutStrategy : public LayoutStrategy {
 public:
  GreedyLayoutStrategy();
  ~GreedyLayoutStrategy();

  Type getType() const override {
    return GREEDY;
  }

  // Main interface implementation
  PageLayout layoutText(WordProvider& provider, TextRenderer& renderer, const LayoutConfig& config) override;
  void renderPage(const PageLayout& layout, TextRenderer& renderer, const LayoutConfig& config) override;

 public:
  // Test-only public wrapper to exercise internal line layout helpers from unit tests.
  // Delegates to the strategy-specific getNextLine
  std::vector<LayoutStrategy::Word> test_getNextLine(WordProvider& provider, TextRenderer& renderer, int16_t maxWidth,
                                                     bool& isParagraphEnd);
  Paragraph test_layoutParagraph(WordProvider& provider, TextRenderer& renderer, int16_t maxWidth);
};

#endif
