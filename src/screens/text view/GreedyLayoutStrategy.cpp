#include "GreedyLayoutStrategy.h"

#include "TextRenderer.h"

#ifdef ARDUINO
#include <Arduino.h>
#else
#include "../test/Arduino.h"
extern unsigned long millis();
#endif

GreedyLayoutStrategy::GreedyLayoutStrategy() : spaceWidth_(4) {}

GreedyLayoutStrategy::~GreedyLayoutStrategy() {}

void GreedyLayoutStrategy::layoutText(const String& text, TextRenderer& renderer, const LayoutConfig& config) {
  const int16_t maxWidth = config.pageWidth - config.marginLeft - config.marginRight;

#ifdef DEBUG_LAYOUT
  Serial.printf("[KP] layoutText called: text length=%d, maxWidth=%d\n", text.length(), maxWidth);
#endif

  // Split text into paragraphs (separated by double newlines)
  std::vector<String> paragraphs;
  int startIdx = 0;

  while (startIdx < text.length()) {
    // Skip any leading whitespace
    while (startIdx < text.length() && (text[startIdx] == ' ' || text[startIdx] == '\n')) {
      startIdx++;
    }

    if (startIdx >= text.length())
      break;

    // Find the end of this paragraph (double newline or end of text)
    int endIdx = startIdx;
    int consecutiveNewlines = 0;

    while (endIdx < text.length()) {
      if (text[endIdx] == '\n') {
        consecutiveNewlines++;
        if (consecutiveNewlines >= 2) {
          break;
        }
      } else if (text[endIdx] != ' ') {
        consecutiveNewlines = 0;
      }
      endIdx++;
    }

    // Extract paragraph
    String paragraph = text.substring(startIdx, endIdx);
    paragraph.trim();
    if (paragraph.length() > 0) {
      paragraphs.push_back(paragraph);
    }

    startIdx = endIdx;
  }

  // Layout each paragraph independently
  int16_t y = config.marginTop;
  const int16_t maxY = config.pageHeight - config.marginBottom;

  // Set space width parameter
  spaceWidth_ = config.minSpaceWidth;

  for (size_t p = 0; p < paragraphs.size() && y < maxY; p++) {
    // Tokenize and measure words
    std::vector<Word> words = tokenizeAndMeasure(paragraphs[p], renderer);

    if (words.empty())
      continue;

    // Use line breaking algorithm for all paragraphs
    y = layoutAndRender(words, renderer, config.marginLeft, y, maxWidth, config.lineHeight, maxY, config.alignment);

    // Add spacing between paragraphs
    if (p < paragraphs.size() - 1) {
      y += config.lineHeight / 2;
    }
  }
}

std::vector<LayoutStrategy::Word> GreedyLayoutStrategy::tokenizeAndMeasure(const String& paragraph,
                                                                           TextRenderer& renderer) {
  std::vector<Word> words;
  int wordStart = 0;

  while (wordStart < paragraph.length()) {
    // Skip whitespace
    while (wordStart < paragraph.length() && (paragraph[wordStart] == ' ' || paragraph[wordStart] == '\n')) {
      wordStart++;
    }

    if (wordStart >= paragraph.length())
      break;

    // Find next word
    int wordEnd = wordStart;
    while (wordEnd < paragraph.length() && paragraph[wordEnd] != ' ' && paragraph[wordEnd] != '\n') {
      wordEnd++;
    }

    if (wordEnd > wordStart) {
      String word = paragraph.substring(wordStart, wordEnd);

      // Measure word width
      int16_t x1, y1;
      uint16_t w, h;
      renderer.getTextBounds(word.c_str(), 0, 0, &x1, &y1, &w, &h);
      int16_t actualWidth = x1 + w;

      words.push_back({word, actualWidth});
    }

    wordStart = wordEnd;
  }

  return words;
}

int16_t GreedyLayoutStrategy::layoutAndRender(const std::vector<Word>& words, TextRenderer& renderer, int16_t x,
                                              int16_t y, int16_t maxWidth, int16_t lineHeight, int16_t maxY,
                                              TextAlignment alignment) {
  if (words.empty()) {
    return y;
  }

  // Calculate line breaks
  std::vector<size_t> breaks = calculateBreaks(words, maxWidth);

  // Render lines
  size_t lineStart = 0;

  // If no breaks, render all words on one line
  if (breaks.empty()) {
    // Calculate line width for alignment
    int16_t lineWidth = 0;
    for (size_t i = 0; i < words.size(); i++) {
      lineWidth += words[i].width;
      if (i < words.size() - 1) {
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
    for (size_t i = 0; i < words.size(); i++) {
      renderer.setCursor(currentX, y);
      renderer.print(words[i].text);
      currentX += words[i].width;
      if (i < words.size() - 1) {
        currentX += spaceWidth_;
      }
    }

#ifdef DEBUG_LAYOUT
    int16_t rightEdge = currentX;
    Serial.printf("[Layout] Line right edge: %d (xPos=%d, lineWidth=%d)\n", rightEdge, xPos, lineWidth);
#endif

    return y + lineHeight;
  }

  // Render each line
  for (size_t breakIdx = 0; breakIdx <= breaks.size() && y < maxY; breakIdx++) {
    size_t lineEnd = (breakIdx < breaks.size()) ? breaks[breakIdx] : words.size();

    if (lineStart >= lineEnd) {
      break;
    }

    // Calculate line width for alignment
    int16_t lineWidth = 0;
    for (size_t i = lineStart; i < lineEnd; i++) {
      lineWidth += words[i].width;
      if (i < lineEnd - 1) {
        lineWidth += spaceWidth_;
      }
    }

    int16_t xPos = x;
    if (alignment == ALIGN_CENTER) {
      xPos = x + (maxWidth - lineWidth) / 2;
    } else if (alignment == ALIGN_RIGHT) {
      xPos = x + maxWidth - lineWidth;
    }

    // Render words on this line
    int16_t currentX = xPos;
    for (size_t i = lineStart; i < lineEnd; i++) {
      renderer.setCursor(currentX, y);
      renderer.print(words[i].text);
      currentX += words[i].width;
      if (i < lineEnd - 1) {
        currentX += spaceWidth_;
      }
    }

#ifdef DEBUG_LAYOUT
    int16_t rightEdge = currentX;
    Serial.printf("[Layout] Line right edge: %d (xPos=%d, lineWidth=%d)\n", rightEdge, xPos, lineWidth);
#endif

    y += lineHeight;
    lineStart = lineEnd;
  }

  return y;
}

std::vector<size_t> GreedyLayoutStrategy::calculateBreaks(const std::vector<Word>& words, int16_t maxWidth) {
  std::vector<size_t> breaks;

  if (words.empty()) {
    return breaks;
  }

#ifdef DEBUG_LAYOUT
  Serial.printf("[Layout] Greedy word wrap: %d words, maxWidth=%d\n", words.size(), maxWidth);
#endif

  int16_t currentLineWidth = 0;

  for (size_t i = 0; i < words.size(); i++) {
    int16_t wordWidth = words[i].width;
    int16_t spaceWidth = (i > 0) ? spaceWidth_ : 0;

    // Check if adding this word (with space) would exceed the line width
    if (currentLineWidth > 0 && currentLineWidth + spaceWidth + wordWidth > maxWidth) {
      // Word doesn't fit, break to next line
      breaks.push_back(i);
      currentLineWidth = wordWidth;
    } else {
      // Word fits on current line
      currentLineWidth += spaceWidth + wordWidth;
    }
  }

#ifdef DEBUG_LAYOUT
  Serial.printf("[Layout] Found %d line breaks\n", breaks.size());
  for (size_t i = 0; i < breaks.size(); i++) {
    Serial.printf("[Layout]   Break at word %d\n", breaks[i]);
  }
#endif

  return breaks;
}