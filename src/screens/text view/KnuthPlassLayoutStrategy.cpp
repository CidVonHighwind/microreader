#include "KnuthPlassLayoutStrategy.h"

#include "TextRenderer.h"

#ifdef ARDUINO
#include <Arduino.h>
#else
#include "../test/Arduino.h"
extern unsigned long millis();
#endif

#include <cmath>
#include <limits>

KnuthPlassLayoutStrategy::KnuthPlassLayoutStrategy() : spaceWidth_(4) {}

KnuthPlassLayoutStrategy::~KnuthPlassLayoutStrategy() {}

void KnuthPlassLayoutStrategy::layoutText(const String& text, TextRenderer& renderer, const LayoutConfig& config) {
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

    // Use Knuth-Plass line breaking algorithm
    y = layoutAndRender(words, renderer, config.marginLeft, y, maxWidth, config.lineHeight, maxY, config.alignment);

    // Add spacing between paragraphs
    if (p < paragraphs.size() - 1) {
      y += config.lineHeight / 2;
    }
  }
}

std::vector<LayoutStrategy::Word> KnuthPlassLayoutStrategy::tokenizeAndMeasure(const String& paragraph,
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

int16_t KnuthPlassLayoutStrategy::layoutAndRender(const std::vector<Word>& words, TextRenderer& renderer, int16_t x,
                                                  int16_t y, int16_t maxWidth, int16_t lineHeight, int16_t maxY,
                                                  TextAlignment alignment) {
  if (words.empty()) {
    return y;
  }

  // Calculate line breaks using Knuth-Plass algorithm
  std::vector<size_t> breaks = calculateBreaks(words, maxWidth);

  // Render lines
  size_t lineStart = 0;

  // If no breaks, render all words on one line (this is the last line)
  if (breaks.empty()) {
    // Last line: use alignment, no justification
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
    Serial.printf("[Layout] Last line right edge: %d (xPos=%d, lineWidth=%d)\n", rightEdge, xPos, lineWidth);
#endif

    return y + lineHeight;
  }

  // Render each line
  for (size_t breakIdx = 0; breakIdx <= breaks.size() && y < maxY; breakIdx++) {
    size_t lineEnd = (breakIdx < breaks.size()) ? breaks[breakIdx] : words.size();

    if (lineStart >= lineEnd) {
      break;
    }

    bool isLastLine = (breakIdx == breaks.size());
    size_t numWords = lineEnd - lineStart;
    size_t numSpaces = (numWords > 1) ? numWords - 1 : 0;

    if (isLastLine || numSpaces == 0) {
      // Last line: use alignment, no justification
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
      Serial.printf("[Layout] Last line (aligned %s): width=%d\n",
                    alignment == ALIGN_LEFT ? "left" : (alignment == ALIGN_CENTER ? "center" : "right"), lineWidth);
#endif
    } else {
      // Non-last line: justify by distributing space evenly
      int16_t totalWordWidth = 0;
      for (size_t i = lineStart; i < lineEnd; i++) {
        totalWordWidth += words[i].width;
      }

      // Calculate space to distribute between words
      int16_t totalSpaceWidth = maxWidth - totalWordWidth;
      float spacePerGap = (float)totalSpaceWidth / (float)numSpaces;

      // Render justified line
      int16_t currentX = x;
      float accumulatedSpace = 0.0f;

      for (size_t i = lineStart; i < lineEnd; i++) {
        renderer.setCursor(currentX, y);
        renderer.print(words[i].text);
        currentX += words[i].width;

        if (i < lineEnd - 1) {
          accumulatedSpace += spacePerGap;
          int16_t spaceToAdd = (int16_t)accumulatedSpace;
          currentX += spaceToAdd;
          accumulatedSpace -= spaceToAdd;
        }
      }

#ifdef DEBUG_LAYOUT
      Serial.printf("[Layout] Justified line: words=%d, avgSpace=%.2f\n", numWords, spacePerGap);
#endif
    }

    y += lineHeight;
    lineStart = lineEnd;
  }

  return y;
}

std::vector<size_t> KnuthPlassLayoutStrategy::calculateBreaks(const std::vector<Word>& words, int16_t maxWidth) {
  std::vector<size_t> breaks;

  if (words.empty()) {
    return breaks;
  }

#ifdef DEBUG_LAYOUT
  Serial.printf("[Layout] Knuth-Plass word wrap: %d words, maxWidth=%d\n", words.size(), maxWidth);
#endif

  size_t n = words.size();

  // Dynamic programming array: minimum demerits to reach each word
  std::vector<float> minDemerits(n + 1, INFINITY_PENALTY);
  std::vector<int> prevBreak(n + 1, -1);

  // Base case: starting position has 0 demerits
  minDemerits[0] = 0.0f;

  // For each possible starting position
  for (size_t i = 0; i < n; i++) {
    if (minDemerits[i] >= INFINITY_PENALTY) {
      continue;  // This position is unreachable
    }

    // Try to fit words [i, j) on one line
    int16_t lineWidth = 0;
    for (size_t j = i; j < n; j++) {
      // Add word width
      if (j > i) {
        lineWidth += spaceWidth_;  // Add space before word (except first word)
      }
      lineWidth += words[j].width;

      // Check if line is too wide
      if (lineWidth > maxWidth) {
        break;  // Can't fit any more words on this line
      }

      // Calculate badness and demerits for this line
      bool isLastLine = (j == n - 1);
      float badness = calculateBadness(lineWidth, maxWidth);
      float demerits = calculateDemerits(badness, isLastLine);

      // Update minimum demerits to reach position j+1
      float totalDemerits = minDemerits[i] + demerits;
      if (totalDemerits < minDemerits[j + 1]) {
        minDemerits[j + 1] = totalDemerits;
        prevBreak[j + 1] = i;
      }
    }
  }

  // Reconstruct breaks by backtracking
  int pos = n;
  while (pos > 0 && prevBreak[pos] >= 0) {
    breaks.push_back(pos);
    pos = prevBreak[pos];
  }

  // Reverse to get breaks in forward order
  std::reverse(breaks.begin(), breaks.end());

  // Remove the last break (end of text)
  if (!breaks.empty() && breaks.back() == n) {
    breaks.pop_back();
  }

#ifdef DEBUG_LAYOUT
  Serial.printf("[Layout] Found %d line breaks with total demerits: %.2f\n", breaks.size(), minDemerits[n]);
  for (size_t i = 0; i < breaks.size(); i++) {
    Serial.printf("[Layout]   Break at word %d\n", breaks[i]);
  }
#endif

  return breaks;
}

float KnuthPlassLayoutStrategy::calculateBadness(int16_t actualWidth, int16_t targetWidth) {
  if (actualWidth > targetWidth) {
    // Line is too wide - very bad
    return INFINITY_PENALTY;
  }

  if (actualWidth == targetWidth) {
    // Perfect fit
    return 0.0f;
  }

  // Calculate adjustment ratio (how much space needs to be stretched)
  float ratio = (float)(targetWidth - actualWidth) / (float)targetWidth;

  // Badness is cube of ratio (Knuth-Plass formula)
  // This penalizes very loose lines more heavily
  float badness = ratio * ratio * ratio * 100.0f;

  return badness;
}

float KnuthPlassLayoutStrategy::calculateDemerits(float badness, bool isLastLine) {
  if (badness >= INFINITY_PENALTY) {
    return INFINITY_PENALTY;
  }

  // Last line is allowed to be loose without penalty
  if (isLastLine) {
    return 0.0f;
  }

  // Demerits is square of (1 + badness)
  float demerits = (1.0f + badness) * (1.0f + badness);

  return demerits;
}
