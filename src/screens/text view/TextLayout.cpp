#include "TextLayout.h"

#include "GreedyLayoutStrategy.h"
#include "WordProvider.h"
#include "text_renderer/TextRenderer.h"

#ifdef ARDUINO
#include <Arduino.h>
#else
#include "Arduino.h"
#endif

// Define static constants
constexpr int32_t TextLayout::MIN_COST;
constexpr int32_t TextLayout::MAX_COST;

TextLayout::TextLayout() : strategy_(new GreedyLayoutStrategy()) {}

TextLayout::TextLayout(LayoutStrategy* strategy) : strategy_(strategy) {}

TextLayout::~TextLayout() {
  delete strategy_;
}

void TextLayout::setStrategy(LayoutStrategy* strategy) {
  delete strategy_;
  strategy_ = strategy;
}

void TextLayout::layoutText(WordProvider& provider, TextRenderer& renderer, const LayoutConfig& config) {
  unsigned long startTime = millis();

#ifdef DEBUG_LAYOUT
  Serial.printf("[TL] Delegating layout to strategy (provider)\n");
#endif

  // Delegate all layout work to the strategy
  strategy_->layoutText(provider, renderer, config);

  unsigned long totalTime = millis() - startTime;
#ifdef ARDUINO
  Serial.print("Text layout time: ");
  Serial.print(totalTime);
  Serial.println(" ms");
#endif
}
