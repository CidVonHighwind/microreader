#include "GreedyLayoutStrategy.h"

#include "WString.h"
#include "WordProvider.h"
#include "text_renderer/TextRenderer.h"
#ifdef ARDUINO
#include <Arduino.h>
#else
#include "../test/Arduino.h"
extern unsigned long millis();
#endif

GreedyLayoutStrategy::GreedyLayoutStrategy() : spaceWidth_(4) {}

GreedyLayoutStrategy::~GreedyLayoutStrategy() {}

void GreedyLayoutStrategy::layoutText(WordProvider& provider, TextRenderer& renderer, const LayoutConfig& config) {
  const int16_t maxWidth = config.pageWidth - config.marginLeft - config.marginRight;
  const int16_t x = config.marginLeft;
  int16_t y = config.marginTop;
  const int16_t maxY = config.pageHeight - config.marginBottom;
  const TextAlignment alignment = config.alignment;
  spaceWidth_ = config.minSpaceWidth;

#ifdef DEBUG_LAYOUT
  Serial.printf("[Greedy] layoutText (provider) called: maxWidth=%d\n", maxWidth);
#endif

  while (y < maxY) {
    bool isParagraphBreak, isLineBreak;
    std::vector<LayoutStrategy::Word> line = getNextLine(provider, renderer, maxWidth, isParagraphBreak, isLineBreak);

    if (line.empty()) {
      // No more content or we hit a break
      if (isParagraphBreak) {
        // Add paragraph spacing
        y += config.lineHeight / 2;
        if (y >= maxY)
          break;
        // Continue to next iteration to get the next line
        continue;
      } else if (isLineBreak) {
        // No extra spacing for line breaks, just continue
        if (y >= maxY)
          break;
        continue;
      } else {
        // No more content
        break;
      }
    }

    // Render the line
    y = renderLine(line, renderer, x, y, maxWidth, config.lineHeight, alignment);
  }
}

std::vector<LayoutStrategy::Word> GreedyLayoutStrategy::getNextLine(WordProvider& provider, TextRenderer& renderer,
                                                                    int16_t maxWidth, bool& isParagraphBreak,
                                                                    bool& isLineBreak) {
  isParagraphBreak = false;
  isLineBreak = false;

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
    if (word.text == "\n") {
      isLineBreak = true;
      break;
    } else if (word.text == "\n\n") {
      isParagraphBreak = true;
      break;
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
                                                                    int16_t maxWidth, bool& isParagraphBreak,
                                                                    bool& isLineBreak) {
  isParagraphBreak = false;
  isLineBreak = false;

  std::vector<LayoutStrategy::Word> line;
  int16_t currentWidth = 0;

  while (provider.getCurrentIndex() > 0) {
    String text = provider.getPrevWord(renderer);
    if (text.length() == 0)
      break;
    // Measure the rendered width using the renderer
    int16_t bx = 0, by = 0;
    uint16_t bw = 0, bh = 0;
    renderer.getTextBounds(text.c_str(), 0, 0, &bx, &by, &bw, &bh);
    LayoutStrategy::Word word{text, static_cast<int16_t>(bw)};

    // Check for breaks - breaks are returned as special words
    if (word.text == "\n") {
      isLineBreak = true;
      break;
    } else if (word.text == "\n\n") {
      isParagraphBreak = true;
      break;
    }

    // Try to add word to the beginning of the line
    int16_t spaceNeeded = currentWidth > 0 ? spaceWidth_ + word.width : word.width;
    if (currentWidth > 0 && currentWidth + spaceNeeded > maxWidth) {
      // Word doesn't fit, put it back
      provider.ungetWord();
      break;
    }

    line.insert(line.begin(), word);
    currentWidth += spaceNeeded;
  }

  return line;
}

int GreedyLayoutStrategy::getPreviousPageStart(WordProvider& provider, TextRenderer& renderer,
                                               const LayoutConfig& config, int currentEndPosition) {
  // Save current provider state
  int savedPosition = provider.getCurrentIndex();
  Serial.print("Pre start: ");
  Serial.println(savedPosition);

  // Set provider to the end of current page
  provider.setPosition(currentEndPosition);

  const int16_t maxWidth = config.pageWidth - config.marginLeft - config.marginRight;
  spaceWidth_ = config.minSpaceWidth;

  // Calculate how many lines fit on the screen
  const int16_t availableHeight = config.pageHeight - config.marginTop - config.marginBottom;
  const int maxLines = availableHeight / config.lineHeight;

  // Go backwards, laying out lines in reverse order
  // Stop after reaching a paragraph break
  int linesBack = 0;

  while (provider.getCurrentIndex() > 0) {
    bool isParagraphBreak;
    bool isLineBreak;
    std::vector<LayoutStrategy::Word> line = getPrevLine(provider, renderer, maxWidth, isParagraphBreak, isLineBreak);

    // print out the line for debugging
    Serial.print("[Layout] Previous line: ");
    for (const auto& word : line) {
      Serial.print(word.text);
      Serial.print("-");
    }

    linesBack++;
    if (isParagraphBreak)
      linesBack++;

    // Stop if we hit a paragraph break
    if (isParagraphBreak && linesBack >= maxLines) {
      break;
    }
  }

  Serial.print("linesBack: ");
  Serial.println(linesBack);

  // Now we have the position after going backwards to the paragraph break
  int positionAtBreak = provider.getCurrentIndex();
  int linesMoved = 0;

  // Move forward the difference between screen lines and lines we moved back
  int linesToMoveForward = linesBack - maxLines;
  if (linesToMoveForward > 0) {
    // Move forward by laying out that many lines
    provider.setPosition(positionAtBreak);

    while (provider.hasNextWord() && linesMoved < linesToMoveForward) {
      bool dummyParagraphBreak, dummyLineBreak;
      std::vector<LayoutStrategy::Word> line =
          getNextLine(provider, renderer, maxWidth, dummyParagraphBreak, dummyLineBreak);

      linesMoved++;
      if (dummyParagraphBreak)
        linesMoved++;
    }
  }

  Serial.print("linesMoved: ");
  Serial.println(linesMoved);

  // The current position is where the previous page starts
  int previousPageStart = provider.getCurrentIndex();

  // Restore provider state
  provider.setPosition(savedPosition);

  Serial.print("Page start: ");
  Serial.println(previousPageStart);

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

#ifdef DEBUG_LAYOUT
  Serial.printf("[Layout] Rendered line: width=%d, words=%d\n", lineWidth, line.size());
#endif

  return y + lineHeight;
}