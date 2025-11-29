#include "GreedyLayoutStrategy.h"

#include "WString.h"
#include "WordProvider.h"
#include "text_renderer/TextRenderer.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif
#ifndef ARDUINO
#include "../../../test/platform_stubs.h"
#endif
#include <cmath>

GreedyLayoutStrategy::GreedyLayoutStrategy() {}

GreedyLayoutStrategy::~GreedyLayoutStrategy() {}

int GreedyLayoutStrategy::layoutText(WordProvider& provider, TextRenderer& renderer, const LayoutConfig& config) {
  const int16_t maxWidth = config.pageWidth - config.marginLeft - config.marginRight;
  const int16_t x = config.marginLeft;
  int16_t y = config.marginTop;
  const int16_t maxY = config.pageHeight - config.marginBottom;
  const TextAlignment alignment = config.alignment;

  // Measure space width using renderer
  renderer.getTextBounds(" ", 0, 0, nullptr, nullptr, &spaceWidth_, nullptr);

  // Serial.printf("[Greedy] layoutText (provider) called: spaceWidth_=%d, maxWidth=%d\n", spaceWidth_, maxWidth);

  int startIndex = provider.getCurrentIndex();
  while (y < maxY) {
    bool isParagraphEnd = false;
    std::vector<LayoutStrategy::Word> line = getNextLine(provider, renderer, maxWidth, isParagraphEnd);

    // Render the line
    y = renderLine(line, renderer, x, y, maxWidth, config.lineHeight, alignment);
  }
  int endIndex = provider.getCurrentIndex();
  // reset the provider to the start index
  provider.setPosition(startIndex);

  return endIndex;
}

int16_t GreedyLayoutStrategy::renderLine(const std::vector<LayoutStrategy::Word>& line, TextRenderer& renderer,
                                         int16_t x, int16_t y, int16_t maxWidth, int16_t lineHeight,
                                         TextAlignment alignment) {
  if (line.empty()) {
    return y + lineHeight;
  }

  // Calculate line width
  int16_t lineWidth = 0;
  for (size_t i = 0; i < line.size(); i++) {
    lineWidth += line[i].width;
    if (i < line.size() - 1) {
      lineWidth += spaceWidth_;
    }
  }

  int16_t xPos = x;
  if (alignment == ALIGN_CENTER) {
    xPos = x + (maxWidth - lineWidth) / 2;
  } else if (alignment == ALIGN_RIGHT) {
    xPos = x + maxWidth - lineWidth;
  }

  int16_t currentX = xPos;
  for (size_t i = 0; i < line.size(); i++) {
    renderer.setCursor(currentX, y);
    renderer.print(line[i].text);
    currentX += line[i].width;
    if (i < line.size() - 1) {
      currentX += spaceWidth_;
    }
  }

  return y + lineHeight;
}

// Test wrappers
std::vector<LayoutStrategy::Word> GreedyLayoutStrategy::test_getNextLine(WordProvider& provider, TextRenderer& renderer,
                                                                         int16_t maxWidth, bool& isParagraphEnd) {
  return getNextLine(provider, renderer, maxWidth, isParagraphEnd);
}