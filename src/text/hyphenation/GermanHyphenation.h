#ifndef GERMAN_HYPHENATION_H
#define GERMAN_HYPHENATION_H

#include "HyphenationStrategy.h"

class GermanHyphenation : public HyphenationStrategy {
 public:
  // German defaults: hyphenation is more common — allow shorter words and smaller left fragments
  // Recommended: min_word_length=5, min_left=2, min_right=3
  std::vector<size_t> hyphenate(const std::string& word, size_t minWordLength = 5, size_t minLeft = 2,
                                size_t minRight = 3) override;

  Language getLanguage() const override {
    return Language::GERMAN;
  }
};

#endif  // GERMAN_HYPHENATION_H
