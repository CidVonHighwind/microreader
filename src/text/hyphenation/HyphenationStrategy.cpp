#include "HyphenationStrategy.h"

#include "EnglishHyphenation.h"
#include "GermanHyphenation.h"

// Implementation of the base class method
std::vector<int> HyphenationStrategy::findHyphenPositions(const std::string& word, size_t minWordLength, size_t minLeft,
                                                          size_t minRight) {
  std::vector<int> positions;

  // First, find existing hyphens in the text
  for (size_t i = 0; i < word.length(); i++) {
    if (word[i] == '-') {
      positions.push_back(static_cast<int>(i));
    }
  }

  // Add algorithmic hyphenation positions for words without existing hyphens
  if (positions.empty()) {
    // Use the language-specific hyphenation strategy (call member, not global hyphenate)
    std::vector<size_t> algorithmicPositions = this->hyphenate(word, minWordLength, minLeft, minRight);

    // Store as negative values to indicate these are algorithmic positions
    // (need hyphen insertion). Offset by -1 so position 0 becomes -1, etc.
    for (size_t bytePos : algorithmicPositions) {
      positions.push_back(-(static_cast<int>(bytePos) + 1));
    }
  }

  return positions;
}

/**
 * Factory function implementation
 */
HyphenationStrategy* createHyphenationStrategy(Language language) {
  switch (language) {
    case Language::BASIC:
      return new ExistingHyphensOnly();
    case Language::ENGLISH:
      return new EnglishHyphenation();
    case Language::GERMAN:
      return new GermanHyphenation();
    case Language::NONE:
    default:
      return new NoHyphenation();
  }
}
