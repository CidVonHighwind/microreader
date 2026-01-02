#ifndef HYPHENATION_STRATEGY_H
#define HYPHENATION_STRATEGY_H

#include <string>
#include <vector>

/**
 * Supported languages for hyphenation
 */
enum class Language {
  NONE,     // No hyphenation at all
  BASIC,    // Only split on existing hyphens in text
  ENGLISH,  // English hyphenation (not yet implemented)
  GERMAN,   // German hyphenation
  // Add more languages here as needed
};

/**
 * Abstract interface for language-specific hyphenation strategies.
 * Each language implementation should provide its own algorithm
 * for finding valid hyphenation points in words.
 */
class HyphenationStrategy {
 public:
  virtual ~HyphenationStrategy() = default;

  /**
   * Find hyphenation positions in a word.
   * Returns byte positions where hyphens can be inserted.
   *
   * @param word The word to hyphenate (UTF-8 encoded)
   * @param minWordLength Minimum word length to consider for hyphenation
   * @param minFragmentLength Minimum characters that must remain on each side of a split
   * @return Vector of byte positions where hyphenation is allowed
   */
  // Hyphenate with separate minima for left and right fragments.
  // @param minWordLength: minimum word length to consider for any hyphenation
  // @param minLeft: minimum characters before the hyphen
  // @param minRight: minimum characters after the hyphen
  virtual std::vector<size_t> hyphenate(const std::string& word, size_t minWordLength = 6, size_t minLeft = 3,
                                        size_t minRight = 3) = 0;

  /**
   * Find all hyphen positions in a word (both existing and algorithmic).
   * Existing hyphens are returned as positive positions.
   * Algorithmic hyphenation positions are returned as negative (-(position + 1)).
   *
   * @param word The word to check (UTF-8 encoded)
   * @param minWordLength Minimum word length to consider for algorithmic hyphenation
   * @param minFragmentLength Minimum characters on each side for algorithmic splits
   * @return Vector of positions (positive = existing, negative = algorithmic)
   */
  std::vector<int> findHyphenPositions(const std::string& word, size_t minWordLength = 6, size_t minLeft = 3,
                                       size_t minRight = 3);

  /**
   * Get the language this strategy handles
   */
  virtual Language getLanguage() const = 0;
};

/**
 * No-op hyphenation strategy that returns no hyphenation points at all.
 * This completely disables hyphenation, including splitting on existing hyphens.
 */
class NoHyphenation : public HyphenationStrategy {
 public:
  std::vector<size_t> hyphenate(const std::string& word, size_t minWordLength = 6, size_t minLeft = 3,
                                size_t minRight = 3) override {
    return std::vector<size_t>();  // No algorithmic hyphenation points
  }

  // Override to prevent splitting even on existing hyphens
  std::vector<int> findHyphenPositions(const std::string& word, size_t minWordLength = 6, size_t minLeft = 3,
                                       size_t minRight = 3) {
    return std::vector<int>();  // No hyphenation at all
  }

  Language getLanguage() const override {
    return Language::NONE;
  }
};

/**
 * Basic hyphenation strategy that only splits on existing hyphens.
 * Does not perform algorithmic hyphenation.
 */
class ExistingHyphensOnly : public HyphenationStrategy {
 public:
  std::vector<size_t> hyphenate(const std::string& word, size_t minWordLength = 6, size_t minLeft = 3,
                                size_t minRight = 3) override {
    return std::vector<size_t>();  // No algorithmic hyphenation, only existing hyphens
  }

  Language getLanguage() const override {
    return Language::BASIC;
  }
};

/**
 * Factory function to create appropriate hyphenation strategy for a language
 */
HyphenationStrategy* createHyphenationStrategy(Language language);

#endif
