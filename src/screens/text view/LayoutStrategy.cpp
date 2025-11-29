#include "LayoutStrategy.h"

#include "WString.h"
#include "WordProvider.h"
#include "text_renderer/TextRenderer.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <cmath>

std::vector<LayoutStrategy::Word> LayoutStrategy::getNextLine(WordProvider& provider, TextRenderer& renderer,
                                                              int16_t maxWidth, bool& isParagraphEnd) {
  isParagraphEnd = false;

  std::vector<LayoutStrategy::Word> line;
  int16_t currentWidth = 0;

  while (provider.hasNextWord()) {
    String text = provider.getNextWord(renderer);
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

std::vector<LayoutStrategy::Word> LayoutStrategy::getPrevLine(WordProvider& provider, TextRenderer& renderer,
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

int LayoutStrategy::getPreviousPageStart(WordProvider& provider, TextRenderer& renderer, const LayoutConfig& config,
                                         int currentStartPosition) {
  // Save current provider state
  int savedPosition = provider.getCurrentIndex();

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

  // Go backwards, laying out lines in reverse order
  // Stop after reaching a paragraph break
  int linesBack = 0;
  // position after going backwards to the paragraph break
  int positionAtBreak = 0;

  while (provider.getCurrentIndex() > 0) {
    bool isParagraphEnd;
    std::vector<LayoutStrategy::Word> line = getPrevLine(provider, renderer, maxWidth, isParagraphEnd);

    linesBack++;

    // Stop if we hit a paragraph break
    if (isParagraphEnd && linesBack >= maxLines) {
      positionAtBreak = provider.getCurrentIndex() + 1;
      provider.setPosition(positionAtBreak);
      break;
    }
  }

  int linesMoved = 0;

  // Move forward the difference between screen lines and lines we moved back
  int linesToMoveForward = linesBack - maxLines;
  if (linesToMoveForward > 0) {
    while (provider.hasNextWord() && linesMoved < linesToMoveForward) {
      bool dummyParagraphEnd;
      std::vector<LayoutStrategy::Word> line = getNextLine(provider, renderer, maxWidth, dummyParagraphEnd);
      linesMoved++;
    }
  }

  // The current position is where the previous page starts
  int previousPageStart = provider.getCurrentIndex();

  // Restore provider state
  provider.setPosition(savedPosition);

  return previousPageStart;
}

std::vector<LayoutStrategy::Word> LayoutStrategy::test_getPrevLine(WordProvider& provider, TextRenderer& renderer,
                                                                   int16_t maxWidth, bool& isParagraphEnd) {
  return getPrevLine(provider, renderer, maxWidth, isParagraphEnd);
}

std::vector<LayoutStrategy::Word> LayoutStrategy::test_getNextLineDefault(WordProvider& provider,
                                                                          TextRenderer& renderer, int16_t maxWidth,
                                                                          bool& isParagraphEnd) {
  return getNextLine(provider, renderer, maxWidth, isParagraphEnd);
}

int LayoutStrategy::test_getPreviousPageStart(WordProvider& provider, TextRenderer& renderer,
                                              const LayoutConfig& config, int currentStartPosition) {
  return getPreviousPageStart(provider, renderer, config, currentStartPosition);
}
