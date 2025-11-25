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

  Serial.printf("[Greedy] layoutText (provider) called: spaceWidth_=%d, maxWidth=%d\n", spaceWidth_, maxWidth);

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

std::vector<LayoutStrategy::Word> GreedyLayoutStrategy::getNextLine(WordProvider& provider, TextRenderer& renderer,
                                                                    int16_t maxWidth, bool& isParagraphEnd) {
  isParagraphEnd = false;

  std::vector<LayoutStrategy::Word> line;
  int16_t currentWidth = 0;

  while (provider.hasNextWord()) {
    String text = provider.getNextWord(renderer);
    // Measure the rendered width using the renderer
    int16_t bx = 0, by = 0;
    uint16_t bw = 0, bh = 0;
    renderer.getTextBounds(text.c_str(), 0, 0, &bx, &by, &bw, &bh);
    LayoutStrategy::Word word{text, static_cast<int16_t>(bw)};

    // Check for breaks - breaks are returned as special words
    if (word.text == String("\n")) {
      isParagraphEnd = true;
      break;
    }
    if (word.text[0] == ' ') {
      continue;
    }

    // Calculate space needed for this word
    int16_t spaceNeeded = currentWidth > 0 ? spaceWidth_ + word.width : word.width;

    if (currentWidth > 0 && currentWidth + spaceNeeded > maxWidth) {
      // Word doesn't fit, put it back and end line
      provider.ungetWord();
      break;
    } else {
      // Word fits, add to line
      line.push_back(word);
      currentWidth += spaceNeeded;
    }
  }

  return line;
}

std::vector<LayoutStrategy::Word> GreedyLayoutStrategy::getPrevLine(WordProvider& provider, TextRenderer& renderer,
                                                                    int16_t maxWidth, bool& isParagraphEnd) {
  isParagraphEnd = false;

  std::vector<LayoutStrategy::Word> line;
  int16_t currentWidth = 0;

  while (provider.getCurrentIndex() > 0) {
    String text = provider.getPrevWord(renderer);
    // Measure the rendered width using the renderer
    int16_t bx = 0, by = 0;
    uint16_t bw = 0, bh = 0;
    renderer.getTextBounds(text.c_str(), 0, 0, &bx, &by, &bw, &bh);
    LayoutStrategy::Word word{text, static_cast<int16_t>(bw)};

    // Check for breaks - breaks are returned as special words
    if (word.text == String("\n")) {
      isParagraphEnd = true;
      break;
    }
    if (word.text[0] == ' ') {
      continue;
    }

    // Try to add word to the beginning of the line
    int16_t spaceNeeded = currentWidth > 0 ? spaceWidth_ + word.width : word.width;
    if (currentWidth > 0 && currentWidth + spaceNeeded > maxWidth) {
      // Word doesn't fit, put it back
      provider.ungetWord();
      break;
    } else {
      line.insert(line.begin(), word);
      currentWidth += spaceNeeded;
    }
  }

  return line;
}

int GreedyLayoutStrategy::getPreviousPageStart(WordProvider& provider, TextRenderer& renderer,
                                               const LayoutConfig& config, int currentStartPosition) {
  // Save current provider state
  int savedPosition = provider.getCurrentIndex();
  Serial.print("Pre start: ");
  Serial.println(savedPosition);

  // Set provider to the end of current page
  provider.setPosition(currentStartPosition);
  String word = provider.getPrevWord(renderer);
  if (word != String("\n")) {
    provider.ungetWord();
  }

  const int16_t maxWidth = config.pageWidth - config.marginLeft - config.marginRight;
  renderer.getTextBounds(" ", 0, 0, nullptr, nullptr, &spaceWidth_, nullptr);

  // Calculate how many lines fit on the screen
  const int16_t availableHeight = config.pageHeight - config.marginTop - config.marginBottom;
  const int maxLines = ceil(availableHeight / (double)config.lineHeight);

  // Debug output
  Serial.printf("[Greedy] getPreviousPageStart called: spaceWidth_=%d, maxWidth=%d, maxLines=%d\n", spaceWidth_,
                maxWidth, maxLines);

  // Go backwards, laying out lines in reverse order
  // Stop after reaching a paragraph break
  int linesBack = 0;
  // position after going backwards to the paragraph break
  int positionAtBreak = 0;

  while (provider.getCurrentIndex() > 0) {
    bool isParagraphEnd;
    std::vector<LayoutStrategy::Word> line = getPrevLine(provider, renderer, maxWidth, isParagraphEnd);

    // // print out the line for debugging
    // Serial.print("[Layout] Previous line at position ");
    // Serial.print(provider.getCurrentIndex());
    // Serial.print(": ");
    // for (const auto& word : line) {
    //   Serial.print("\"");
    //   Serial.print(word.text);
    //   Serial.print("\"");

    //   // reached the end
    //   if (&word != &line.back())
    //     Serial.print(" - ");
    // }
    // Serial.println();

    linesBack++;

    // Stop if we hit a paragraph break
    if (isParagraphEnd && linesBack >= maxLines) {
      positionAtBreak = provider.getCurrentIndex() + 1;
      provider.setPosition(positionAtBreak);
      break;
    }
  }

  // Serial.print("linesBack: ");
  // Serial.println(linesBack);

  int linesMoved = 0;

  // Move forward the difference between screen lines and lines we moved back
  int linesToMoveForward = linesBack - maxLines;
  if (linesToMoveForward > 0) {
    while (provider.hasNextWord() && linesMoved < linesToMoveForward) {
      bool dummyParagraphEnd;
      std::vector<LayoutStrategy::Word> line = getNextLine(provider, renderer, maxWidth, dummyParagraphEnd);

      // Serial.print("[Layout] Backward line at position ");
      // Serial.print(provider.getCurrentIndex());
      // Serial.print(": ");
      // for (const auto& word : line) {
      //   Serial.print("\"");
      //   Serial.print(word.text);
      //   Serial.print("\"");

      //   // reached the end
      //   if (&word != &line.back())
      //     Serial.print(" - ");
      // }
      // Serial.println();

      linesMoved++;
    }
  }

  // Serial.print("linesMoved: ");
  // Serial.println(linesMoved);

  // The current position is where the previous page starts
  int previousPageStart = provider.getCurrentIndex();

  // Restore provider state
  provider.setPosition(savedPosition);

  // Serial.print("Page start: ");
  // Serial.println(previousPageStart);

  return previousPageStart;
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

std::vector<LayoutStrategy::Word> GreedyLayoutStrategy::test_getPrevLine(WordProvider& provider, TextRenderer& renderer,
                                                                         int16_t maxWidth, bool& isParagraphEnd) {
  return getPrevLine(provider, renderer, maxWidth, isParagraphEnd);
}