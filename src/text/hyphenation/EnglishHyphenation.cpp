#include "EnglishHyphenation.h"

#include "Liang/hyph-en-us.h"
#include "Liang/hyphenation.h"

std::vector<size_t> EnglishHyphenation::hyphenate(const std::string& word, size_t minWordLength, size_t minLeft,
                                                  size_t minRight) {
  const int MAX_POSITIONS = 32;
  size_t out_positions[MAX_POSITIONS];

  // Do not hyphenate words shorter than the minimum word length
  if (word.length() < minWordLength) {
    return std::vector<size_t>();
  }

  int count = liang_hyphenate(word.c_str(), minLeft, minRight, '.', out_positions, MAX_POSITIONS, en_us_patterns);

  std::vector<size_t> positions;
  if (count > 0) {
    positions.assign(out_positions, out_positions + count);
  }

  return positions;
}