#include "LayoutStrategy.h"

#include "../../content/providers/WordProvider.h"
#include "../../rendering/TextRenderer.h"
#include "../hyphenation/GermanHyphenation.h"
#include "../hyphenation/HyphenationStrategy.h"
#include "WString.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <cmath>

LayoutStrategy::LayoutStrategy() : hyphenationStrategy_(new NoHyphenation()) {}

LayoutStrategy::~LayoutStrategy() {
  delete hyphenationStrategy_;
}

void LayoutStrategy::setLanguage(Language language) {
  if (hyphenationStrategy_) {
    delete hyphenationStrategy_;
  }
  hyphenationStrategy_ = createHyphenationStrategy(language);
}

std::vector<LayoutStrategy::Word> LayoutStrategy::getNextLine(WordProvider& provider, TextRenderer& renderer,
                                                              int16_t maxWidth, bool& isParagraphEnd) {
  isParagraphEnd = false;

  std::vector<LayoutStrategy::Word> line;
  int16_t currentWidth = 0;

  while (provider.hasNextWord()) {
    int wordStartIndex = provider.getCurrentIndex();
    String text = provider.getNextWord();
    int16_t bx = 0, by = 0;
    uint16_t bw = 0, bh = 0;
    renderer.getTextBounds(text.c_str(), 0, 0, &bx, &by, &bw, &bh);
    LayoutStrategy::Word word{text, static_cast<int16_t>(bw), 0, 0, false};

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

    if (currentWidth + spaceNeeded > maxWidth) {
      // Word doesn't fit, try to split it at a hyphen
      int16_t availableWidth = maxWidth - currentWidth - spaceWidth_;
      HyphenSplit split = {-1, false, false};

      // if (allowHyphenation)
      { split = findBestHyphenSplitForward(word.text, availableWidth, renderer); }
      if (split.found) {
        // Successfully found a split position
        String firstPart;
        if (split.isAlgorithmic) {
          // Add hyphen for algorithmic split
          firstPart = word.text.substring(0, split.position) + "-";
        } else {
          // Include existing hyphen
          firstPart = word.text.substring(0, split.position + 1);
        }

        int16_t bx2 = 0, by2 = 0;
        uint16_t bw2 = 0, bh2 = 0;
        renderer.getTextBounds(firstPart.c_str(), 0, 0, &bx2, &by2, &bw2, &bh2);
        line.push_back({firstPart, static_cast<int16_t>(bw2), 0, 0, true});  // wasSplit = true

        // Move provider position: consume characters up to the split point
        // For existing hyphens, include the hyphen character (+1)
        provider.setPosition(wordStartIndex);
        provider.consumeChars(split.position + (split.isAlgorithmic ? 0 : 1));
        break;
      } else if (currentWidth > 0) {
        // Can't split, put it back and end line
        provider.ungetWord();
        break;
      }
    } else {
      // Word fits, add to line
      line.push_back(word);
      currentWidth += spaceNeeded;
    }
  }

  // // output the words in the line
  // for (size_t i = 0; i < line.size(); i++) {
  //   Serial.printf(line[i].text.c_str());
  //   Serial.print(";");
  // }
  // Serial.println();

  return line;
}

std::vector<LayoutStrategy::Word> LayoutStrategy::getPrevLine(WordProvider& provider, TextRenderer& renderer,
                                                              int16_t maxWidth, bool& isParagraphEnd) {
  isParagraphEnd = false;
  std::vector<LayoutStrategy::Word> line;
  int16_t currentWidth = 0;
  bool firstWord = true;

  while (provider.getCurrentIndex() > 0) {
    int wordEndIndex = provider.getCurrentIndex();
    String text = provider.getPrevWord();
    int wordStartIndex = provider.getCurrentIndex();
    bool isFirstWord = firstWord;
    firstWord = false;

    // Measure the rendered width using the renderer
    int16_t bx = 0, by = 0;
    uint16_t bw = 0, bh = 0;
    renderer.getTextBounds(text.c_str(), 0, 0, &bx, &by, &bw, &bh);
    LayoutStrategy::Word word{text, static_cast<int16_t>(bw), 0, 0, false};

    // Check for breaks - breaks are returned as special words
    if (word.text == String("\n")) {
      // check if we are at an empty line or at the start of a paragraph
      if (isFirstWord) {
        String prevWord = provider.getPrevWord();
        provider.ungetWord();
        if (prevWord == String("\n")) {
          isParagraphEnd = true;
          break;
        }
        // we are at the start of the paragraph
        continue;
      } else {
        provider.ungetWord();
        isParagraphEnd = true;
        break;
      }
    }

    if (word.text[0] == ' ') {
      continue;
    }

    // Try to add word to the beginning of the line
    int16_t spaceNeeded = currentWidth > 0 ? spaceWidth_ + word.width : word.width;
    if (currentWidth + spaceNeeded > maxWidth) {
      // Word doesn't fit, try to split it at a hyphen
      int16_t availableWidth = maxWidth - currentWidth - spaceWidth_;
      HyphenSplit split = {-1, false, false};

      // if (allowHyphenation)
      { split = findBestHyphenSplitBackward(word.text, availableWidth, renderer); }
      if (split.found) {
        // Successfully found a split position - add second part (after the split)
        // Take text after the split point
        String secondPart = word.text.substring(split.position, word.text.length());
        int16_t bx2 = 0, by2 = 0;
        uint16_t bw2 = 0, bh2 = 0;
        renderer.getTextBounds(secondPart.c_str(), 0, 0, &bx2, &by2, &bw2, &bh2);
        line.insert(line.begin(), {secondPart, static_cast<int16_t>(bw2), 0, 0, false});

        // Move provider position to the split point by consuming characters from word start
        provider.setPosition(wordStartIndex);
        provider.consumeChars(split.position);
        break;
      } else if (currentWidth > 0) {
        // Can't split, put it back
        provider.ungetWord();
        break;
      }
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

  const int16_t maxWidth = config.pageWidth - config.marginLeft - config.marginRight;
  renderer.getTextBounds(" ", 0, 0, nullptr, nullptr, &spaceWidth_, nullptr);

  // Calculate how many lines fit on the screen
  const int16_t availableHeight = config.pageHeight - config.marginTop - config.marginBottom;
  const int maxLines = ceil(availableHeight / (double)config.lineHeight);

  // Go backwards more than one page to the end of the paragraph and then move forward to find the start
  provider.setPosition(currentStartPosition);
  int linesBack = 0;

  while (provider.getCurrentIndex() > 0) {
    linesBack++;

    bool isParagraphEnd;
    std::vector<LayoutStrategy::Word> line = getPrevLine(provider, renderer, maxWidth, isParagraphEnd);

    // Stop if we hit a paragraph break and have gone back enough
    if (isParagraphEnd && linesBack >= maxLines * 1.25) {
      break;
    }
  }

  // Now we're positioned far enough back. Move forward, storing the start position of each line
  // until we reach currentStartPosition
  std::vector<int> lineStartPositions;
  lineStartPositions.push_back(provider.getCurrentIndex());

  while (provider.getCurrentIndex() < currentStartPosition && provider.hasNextWord()) {
    int lineStart = provider.getCurrentIndex();
    bool isParagraphEnd;
    std::vector<LayoutStrategy::Word> line = getNextLine(provider, renderer, maxWidth, isParagraphEnd);

    linesBack--;

    if (provider.getCurrentIndex() > lineStart) {
      lineStartPositions.push_back(provider.getCurrentIndex());
    }

    // If we've reached or passed the current start position, stop
    if (provider.getCurrentIndex() >= currentStartPosition) {
      break;
    }
  }

  // Find the line start that is exactly maxLines before currentStartPosition
  int previousPageStart = 0;

  // Find which line index corresponds to currentStartPosition
  int currentLineIndex = -1;
  for (int i = 0; i < lineStartPositions.size(); i++) {
    if (lineStartPositions[i] >= currentStartPosition) {
      currentLineIndex = i;
      break;
    }
  }

  // The previous page should start maxLines lines before the current page
  if (currentLineIndex >= 0) {
    int targetIndex = currentLineIndex - maxLines;
    if (targetIndex >= 0 && targetIndex < lineStartPositions.size()) {
      previousPageStart = lineStartPositions[targetIndex];
    } else if (targetIndex < 0) {
      // We don't have enough lines before, use the first available position
      previousPageStart = lineStartPositions[0];
    }
  } else {
    // Fallback: couldn't find current position in our forward scan
    // This shouldn't happen, but return beginning if it does
    previousPageStart = lineStartPositions[0];
  }

  // Restore provider state
  provider.setPosition(savedPosition);

  return previousPageStart;
}

LayoutStrategy::HyphenSplit LayoutStrategy::findBestHyphenSplitForward(const String& word, int16_t availableWidth,
                                                                       TextRenderer& renderer) {
  // Find the last (rightmost) hyphen position where the first part fits
  std::vector<int> hyphenPositions;
  if (hyphenationStrategy_) {
    std::string stdWord = word.c_str();
    hyphenPositions = hyphenationStrategy_->findHyphenPositions(stdWord, 6, 3);
  }
  HyphenSplit result = {-1, false, false};

  for (int i = 0; i < hyphenPositions.size(); i++) {
    int pos = hyphenPositions[i];
    bool isAlgorithmic = pos < 0;
    int actualPos = isAlgorithmic ? -(pos + 1) : pos;

    // For algorithmic positions, we need to add a hyphen
    // For existing hyphens, include the hyphen character
    String candidate;
    if (isAlgorithmic) {
      candidate = word.substring(0, actualPos) + "-";
    } else {
      candidate = word.substring(0, actualPos + 1);
    }

    int16_t bx = 0, by = 0;
    uint16_t bw = 0, bh = 0;
    renderer.getTextBounds(candidate.c_str(), 0, 0, &bx, &by, &bw, &bh);

    if (bw <= availableWidth) {
      result = {actualPos, isAlgorithmic, true};  // This hyphen works, keep looking for a later one
    } else {
      break;  // Too wide, stop searching
    }
  }

  return result;
}

LayoutStrategy::HyphenSplit LayoutStrategy::findBestHyphenSplitBackward(const String& word, int16_t availableWidth,
                                                                        TextRenderer& renderer) {
  // Find the earliest (leftmost) hyphen position where the second part fits
  std::vector<int> hyphenPositions;
  if (hyphenationStrategy_) {
    std::string stdWord = word.c_str();
    hyphenPositions = hyphenationStrategy_->findHyphenPositions(stdWord, 6, 3);
  }
  HyphenSplit result = {-1, false, false};

  for (int i = hyphenPositions.size() - 1; i >= 0; i--) {
    int pos = hyphenPositions[i];
    bool isAlgorithmic = pos < 0;
    int actualPos = isAlgorithmic ? -(pos + 1) : pos;

    // For both algorithmic and existing hyphens, take text after the split point
    String candidate = word.substring(actualPos, word.length());
    int16_t bx = 0, by = 0;
    uint16_t bw = 0, bh = 0;
    renderer.getTextBounds(candidate.c_str(), 0, 0, &bx, &by, &bw, &bh);

    if (bw <= availableWidth) {
      result = {actualPos, isAlgorithmic, true};  // This hyphen works, keep looking for an earlier one
    } else {
      break;  // Too wide, stop searching
    }
  }

  return result;
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
