#include "TextLayout.h"

#include "GreedyLayoutStrategy.h"
#include "TextRenderer.h"

#ifdef ARDUINO
#include <Arduino.h>
#else
// Mock Arduino functions for non-Arduino builds
extern unsigned long millis();
struct MockSerial {
  void print(const char*);
  void print(unsigned long);
  void println(const char*);
  void printf(const char*, ...);
};
extern MockSerial Serial;
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

void TextLayout::layoutText(const String& text, TextRenderer& renderer, const LayoutConfig& config) {
  unsigned long startTime = millis();

#ifdef DEBUG_LAYOUT
  Serial.printf("[TL] Delegating layout to strategy\n");
#endif

  // Delegate all layout work to the strategy
  strategy_->layoutText(text, renderer, config);

  unsigned long totalTime = millis() - startTime;
#ifdef ARDUINO
  Serial.print("Text layout time: ");
  Serial.print(totalTime);
  Serial.println(" ms");
#endif
}
