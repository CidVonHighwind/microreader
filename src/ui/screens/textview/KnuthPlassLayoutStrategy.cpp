#include "KnuthPlassLayoutStrategy.h"

#include "../../../rendering/TextRenderer.h"
#include "WString.h"
#include "WordProvider.h"
#ifdef ARDUINO
#include <Arduino.h>
#else
#include "Arduino.h"
extern unsigned long millis();
#endif

#include <algorithm>
#include <cmath>
#include <limits>

#define DEBUG_LAYOUT

KnuthPlassLayoutStrategy::KnuthPlassLayoutStrategy() {}

KnuthPlassLayoutStrategy::~KnuthPlassLayoutStrategy() {}

LayoutStrategy::PageLayout KnuthPlassLayoutStrategy::layoutText(WordProvider& provider, TextRenderer& renderer,
                                                                const LayoutConfig& config) {
  const int16_t maxWidth = config.pageWidth - config.marginLeft - config.marginRight;
  int16_t y = config.marginTop;
  const int16_t maxY = config.pageHeight - config.marginBottom;

  // Measure space width using renderer
  renderer.getTextBounds(" ", 0, 0, nullptr, nullptr, &spaceWidth_, nullptr);

  PageLayout result;
  std::vector<ParagraphLayoutInfo> paragraphs;
  std::vector<LayoutStrategy::Word> words;

  int startIndex = provider.getCurrentIndex();
  while (y < maxY) {
    int16_t yStart = y;
    int16_t lineCount = 0;
    bool isParagraphEnd = false;

    // Collect words for the paragraph
    while (y < maxY && !isParagraphEnd) {
      std::vector<LayoutStrategy::Word> line = getNextLine(provider, renderer, maxWidth, isParagraphEnd);
      y += config.lineHeight;
      lineCount++;

      // iterate line by line until paragraph end
      for (size_t i = 0; i < line.size(); i++) {
        words.push_back(line[i]);
      }
    }

    if (!words.empty()) {
      // Calculate line breaks using Knuth-Plass algorithm
      std::vector<size_t> breaks = calculateBreaks(words, maxWidth);

      if (lineCount != breaks.size() + 1) {
        lineCountMismatch_ = true;
        expectedLineCount_ = lineCount;
        actualLineCount_ = breaks.size() + 1;
      }

      // Convert breaks into lines and calculate positions
      const int16_t x = config.marginLeft;
      const TextAlignment alignment = config.alignment;
      int16_t currentY = yStart;

      size_t lineStart = 0;
      for (size_t breakIdx = 0; breakIdx <= breaks.size(); breakIdx++) {
        size_t lineEnd = (breakIdx < breaks.size()) ? breaks[breakIdx] : words.size();
        if (lineStart >= lineEnd)
          break;

        std::vector<Word> lineWords;
        for (size_t i = lineStart; i < lineEnd; i++) {
          lineWords.push_back(words[i]);
        }

        // Calculate positions for words in this line
        bool isLastLine = (breakIdx == breaks.size()) && isParagraphEnd;
        size_t numWords = lineWords.size();
        size_t numSpaces = (numWords > 1) ? numWords - 1 : 0;

        if (isLastLine || numSpaces == 0) {
          // Last line: use alignment, no justification
          int16_t lineWidth = 0;
          for (size_t i = 0; i < lineWords.size(); i++) {
            lineWidth += lineWords[i].width;
            if (i < lineWords.size() - 1) {
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
          for (size_t i = 0; i < lineWords.size(); i++) {
            lineWords[i].x = currentX;
            lineWords[i].y = currentY;
            currentX += lineWords[i].width;
            if (i < lineWords.size() - 1) {
              currentX += spaceWidth_;
            }
          }
        } else {
          // Non-last line: justify by distributing space evenly
          int16_t totalWordWidth = 0;
          for (size_t i = 0; i < lineWords.size(); i++) {
            totalWordWidth += lineWords[i].width;
          }

          // Calculate space to distribute between words
          int16_t totalSpaceWidth = maxWidth - totalWordWidth;
          float spacePerGap = (float)totalSpaceWidth / (float)numSpaces;

          if (spacePerGap > 16 * spaceWidth_) {
            // Limit maximum space stretch to avoid extreme gaps
            spacePerGap = std::max(spacePerGap * 0.25f, (float)spaceWidth_);
          }

          int16_t currentX = x;
          float accumulatedSpace = 0.0f;

          for (size_t i = 0; i < lineWords.size(); i++) {
            lineWords[i].x = currentX;
            lineWords[i].y = currentY;
            currentX += lineWords[i].width;

            if (i < lineWords.size() - 1) {
              accumulatedSpace += spacePerGap;
              int16_t spaceToAdd = (int16_t)accumulatedSpace;
              currentX += spaceToAdd;
              accumulatedSpace -= spaceToAdd;
            }
          }
        }

        result.lines.push_back(lineWords);
        lineStart = lineEnd;
        currentY += config.lineHeight;
      }

      words.clear();
    }
  }

  result.endPosition = provider.getCurrentIndex();
  // reset the provider to the start index
  provider.setPosition(startIndex);

  return result;
}

void KnuthPlassLayoutStrategy::renderPage(const PageLayout& layout, TextRenderer& renderer,
                                          const LayoutConfig& config) {
  for (const auto& line : layout.lines) {
    for (const auto& word : line) {
      renderer.setCursor(word.x, word.y);
      renderer.print(word.text);
    }
  }
}

std::vector<size_t> KnuthPlassLayoutStrategy::calculateBreaks(const std::vector<Word>& words, int16_t maxWidth) {
  std::vector<size_t> breaks;

  if (words.empty()) {
    return breaks;
  }

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
        // Special case: if this is the first word on the line and it's too wide,
        // we must still place it on its own line to make progress
        if (j == i) {
          // Force this oversized word onto its own line with a high but not infinite penalty
          // Use a large fixed penalty (100) rather than INFINITY_PENALTY to allow progress
          float demerits = 100.0f;

          // Add a constant penalty per line to favor fewer lines
          demerits += 50.0f;

          float totalDemerits = minDemerits[i] + demerits;
          if (totalDemerits < minDemerits[j + 1]) {
            minDemerits[j + 1] = totalDemerits;
            prevBreak[j + 1] = i;
          }
        }
        break;  // Can't fit any more words on this line
      }

      // Calculate badness and demerits for this line
      bool isLastLine = (j == n - 1);
      float badness = calculateBadness(lineWidth, maxWidth);
      float demerits = calculateDemerits(badness, isLastLine);

      // Add a constant penalty per line to favor fewer lines
      // This makes layouts with fewer lines always preferable
      demerits += 50.0f;

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
