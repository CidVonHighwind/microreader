#ifndef ENGLISH_HYPHENATION_H
#define ENGLISH_HYPHENATION_H

#include "HyphenationStrategy.h"

class EnglishHyphenation : public HyphenationStrategy {
 public:
  // English defaults: conservative, avoid breaking short words
  // Recommended: min_word_length=6, min_left=3, min_right=3
  std::vector<size_t> hyphenate(const std::string& word, size_t minWordLength = 6, size_t minLeft = 3,
                                size_t minRight = 3) override;

  Language getLanguage() const override {
    return Language::ENGLISH;
  }
};

#endif  // ENGLISH_HYPHENATION_H