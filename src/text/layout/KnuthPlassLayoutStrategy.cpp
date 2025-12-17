#include "KnuthPlassLayoutStrategy.h"

#include "../../content/providers/WordProvider.h"
#include "../../rendering/TextRenderer.h"
#include "WString.h"
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
  renderer.setFontStyle(FontStyle::REGULAR);
  renderer.getTextBounds(" ", 0, 0, nullptr, nullptr, &spaceWidth_, nullptr);

  PageLayout result;
  std::vector<ParagraphLayoutInfo> paragraphs;
  std::vector<LayoutStrategy::Word> words;

  int startIndex = provider.getCurrentIndex();
  while (y < maxY) {
    int16_t yStart = y;
    int16_t lineCount = 0;
    bool isParagraphEnd = false;
    TextAlignment paragraphAlignment = config.alignment;  // Will be set by first line

    // Collect words for the paragraph
    while (y < maxY && !isParagraphEnd) {
      Line lineResult = getNextLine(provider, renderer, maxWidth, isParagraphEnd, config.alignment);
      y += config.lineHeight;

      // Capture alignment from first line of paragraph
      if (lineCount == 0 && !lineResult.words.empty()) {
        paragraphAlignment = lineResult.alignment;
      }

      // Only count lines that have actual content
      if (!lineResult.words.empty()) {
        lineCount++;
      }

      // iterate line by line until paragraph end
      for (size_t i = 0; i < lineResult.words.size(); i++) {
        words.push_back(lineResult.words[i]);
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

        // Calculate indentation from leading spaces for first line
        int16_t indent = 0;
        if (breakIdx == 0) {
          for (const auto& w : lineWords) {
            if (w.text == " ") {
              indent += w.width;
            } else {
              break;
            }
          }
        }
        const int16_t x = config.marginLeft + indent;
        int16_t effectiveMaxWidth = maxWidth - indent;

        // Trim leading and trailing spaces
        while (!lineWords.empty() && lineWords.front().text == " ") {
          lineWords.erase(lineWords.begin());
        }
        while (!lineWords.empty() && lineWords.back().text == " ") {
          lineWords.erase(lineWords.end() - 1);
        }

        // Calculate positions for words in this line
        bool isLastLine = (breakIdx == breaks.size()) && isParagraphEnd;
        size_t numWords = lineWords.size();
        size_t numSpaceWords = 0;
        for (const auto& w : lineWords) {
          if (w.text == " ")
            numSpaceWords++;
        }

        if (isLastLine || numSpaceWords == 0) {
          // Last line: use alignment, no justification
          int16_t lineWidth = 0;
          for (size_t i = 0; i < lineWords.size(); i++) {
            lineWidth += lineWords[i].width;
          }

          int16_t xPos = x;
          if (paragraphAlignment == ALIGN_CENTER) {
            xPos = x + (effectiveMaxWidth - lineWidth) / 2;
          } else if (paragraphAlignment == ALIGN_RIGHT) {
            xPos = x + effectiveMaxWidth - lineWidth;
          }

          int16_t currentX = xPos;
          for (size_t i = 0; i < lineWords.size(); i++) {
            lineWords[i].x = currentX;
            lineWords[i].y = currentY;
            currentX += lineWords[i].width;
          }
        } else {
          // Non-last line: justify by distributing space evenly among space words
          int16_t totalWordWidth = 0;
          for (size_t i = 0; i < lineWords.size(); i++) {
            totalWordWidth += lineWords[i].width;
          }

          // Calculate space to distribute among space words
          int16_t totalSpaceWidth = maxWidth - totalWordWidth;
          float extraPerSpace = (float)totalSpaceWidth / (float)numSpaceWords;

          if (extraPerSpace > 16 * spaceWidth_) {
            // Limit maximum space stretch to avoid extreme gaps
            extraPerSpace = std::max(extraPerSpace * 0.25f, (float)spaceWidth_);
          }

          // Increase widths of space words
          float accumulatedExtra = 0.0f;
          for (auto& w : lineWords) {
            if (w.text == " ") {
              accumulatedExtra += extraPerSpace;
              int16_t extra = (int16_t)accumulatedExtra;
              w.width += extra;
              accumulatedExtra -= extra;
            }
          }

          // Now place words
          int16_t currentX = x;
          for (size_t i = 0; i < lineWords.size(); i++) {
            lineWords[i].x = currentX;
            lineWords[i].y = currentY;
            currentX += lineWords[i].width;
          }
        }

        Line lineStruct;
        lineStruct.words = lineWords;
        lineStruct.alignment = paragraphAlignment;
        result.lines.push_back(lineStruct);
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
    for (const auto& word : line.words) {
      renderer.setFontStyle(word.style);
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

      // If this word was split (hyphenated), we must break the line here
      if (words[j].wasSplit) {
        break;
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
