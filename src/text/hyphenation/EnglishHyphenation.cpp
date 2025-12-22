#include "EnglishHyphenation.h"

#include <algorithm>
#include <codecvt>
#include <cwctype>
#include <locale>
#include <unordered_set>

namespace {

std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;

char32_t toLowerEnglish(char32_t c) {
  return std::towlower(c);
}

bool isLetter(char32_t c) {
  return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
}

bool isVowel(char32_t c) {
  c = toLowerEnglish(c);
  switch (c) {
    case U'a':
    case U'e':
    case U'i':
    case U'o':
    case U'u':
      return true;
    default:
      return false;
  }
}

// Y is treated as a vowel when it appears after a consonant and before another consonant or end
bool isVowelInContext(char32_t c, size_t pos, const std::u32string& word) {
  if (isVowel(c))
    return true;

  char32_t lower = toLowerEnglish(c);
  if (lower == U'u' && pos > 0) {
    char32_t prev = toLowerEnglish(word[pos - 1]);
    // Treat "qu" as an inseparable consonant cluster
    if (prev == U'q') {
      return false;
    }
  }

  if (lower == U'y' && pos > 0) {
    // Y acts as vowel if preceded by consonant
    char32_t prev = toLowerEnglish(word[pos - 1]);
    if (isLetter(prev) && !isVowel(prev)) {
      return true;
    }
  }
  return false;
}

bool isConsonant(char32_t c) {
  return isLetter(c) && !isVowel(c);
}

bool isAllowedOnset(const std::u32string& onset) {
  // Common English syllable onsets (consonant clusters that can start a syllable)
  static const std::vector<std::u32string> allowed = {
      // Single consonants
      U"b", U"c", U"d", U"f", U"g", U"h", U"j", U"k", U"l", U"m", U"n", U"p", U"q", U"r", U"s", U"t", U"v", U"w", U"x",
      U"y", U"z",
      // Two-consonant clusters
      U"bl", U"br", U"ch", U"cl", U"cr", U"dr", U"dw", U"fl", U"fr", U"gh", U"gl", U"gn", U"gr", U"kn", U"ph", U"pl",
      U"pr", U"qu", U"sc", U"sh", U"sk", U"sl", U"sm", U"sn", U"sp", U"sq", U"st", U"sw", U"th", U"tr", U"tw", U"wh",
      U"wr",
      // Three-consonant clusters
      U"chr", U"sch", U"scr", U"shr", U"sph", U"spl", U"spr", U"squ", U"str", U"thr"};

  return std::find(allowed.begin(), allowed.end(), onset) != allowed.end();
}

bool matchesCluster(const std::u32string& text, size_t start, const std::u32string& cluster) {
  if (start + cluster.size() > text.size()) {
    return false;
  }
  return std::equal(cluster.begin(), cluster.end(), text.begin() + start);
}

std::u32string toLower(const std::u32string& input) {
  std::u32string result;
  result.reserve(input.size());
  for (char32_t c : input) {
    result.push_back(toLowerEnglish(c));
  }
  return result;
}

// Extend inseparable consonant pairs
bool isInseparablePair(const std::u32string& pair) {
  static const std::vector<std::u32string> pairs = {U"ch", U"ck", U"gh", U"gn", U"kn", U"ph", U"sh", U"th",
                                                    U"wh", U"wr", U"rd", U"ld", U"lt", U"nd", U"nt", U"st", U"ng"};
  for (const auto& p : pairs) {
    if (pair == p) {
      return true;
    }
  }
  return false;
}

// Extend prefix and suffix handling with stricter rules
bool isCommonPrefix(const std::u32string& word, size_t pos) {
  static const std::vector<std::u32string> prefixes = {U"re", U"un", U"in", U"dis", U"pre", U"trans", U"align", U"pro"};
  for (const auto& prefix : prefixes) {
    if (pos == prefix.size() && word.substr(0, pos) == prefix) {
      return true;
    }
  }
  return false;
}

// Return the length of the common prefix if the word starts with one, otherwise 0.
size_t findCommonPrefixLength(const std::u32string& word) {
  static const std::vector<std::u32string> prefixes = {U"re", U"un", U"in", U"dis", U"pre", U"trans", U"align", U"pro"};
  for (const auto& prefix : prefixes) {
    if (word.size() >= prefix.size() && word.substr(0, prefix.size()) == prefix) {
      return prefix.size();
    }
  }
  return 0;
}

bool isCommonSuffix(const std::u32string& word, size_t pos) {
  static const std::vector<std::u32string> suffixes = {U"ing", U"ed",   U"er",   U"ly",  U"tion",
                                                       U"ment", U"able", U"ate", U"age"};
  for (const auto& suffix : suffixes) {
    if (pos + suffix.size() == word.size() && word.substr(pos) == suffix) {
      return true;
    }
  }
  return false;
}

// Avoid splitting within prefixes and suffixes
bool isWithinPrefixOrSuffix(const std::u32string& word, size_t pos) {
  return isCommonPrefix(word, pos) || isCommonSuffix(word, pos);
}

}  // namespace

namespace EnglishHyphenation {

std::vector<size_t> hyphenate(const std::string& word) {
  std::u32string wide = converter.from_bytes(word);
  std::u32string lower = toLower(wide);

  std::vector<size_t> positions;
  std::vector<size_t> vowelIndices;

  if (wide.size() < 6) {
    return positions;
  }

  // Find vowel positions (including Y when it acts as vowel)
  for (size_t i = 0; i < lower.size(); ++i) {
    if (lower[i] == U'e' && i == lower.size() - 1) {
      bool previousIsVowel = (i > 0) && isVowelInContext(lower[i - 1], i - 1, lower);
      bool consonantLeEnding = (i > 0 && lower[i - 1] == U'l' &&
                                (i < 2 || !isVowelInContext(lower[i - 2], i - 2, lower)));
      // Skip common silent terminal 'e' unless part of a consonant+'le' ending or a vowel sequence
      if (!previousIsVowel && !consonantLeEnding) {
        continue;
      }
    }

    if (isVowelInContext(lower[i], i, lower)) {
      vowelIndices.push_back(i);
    }
  }

  // Need at least 2 vowels to hyphenate
  if (vowelIndices.size() < 2) {
    return positions;
  }

  // Process consonant clusters between vowels
  for (size_t i = 0; i + 1 < vowelIndices.size(); ++i) {
    size_t leftVowel = vowelIndices[i];
    size_t rightVowel = vowelIndices[i + 1];

    // Skip if vowels are adjacent (diphthong or hiatus)
    if (rightVowel <= leftVowel + 1) {
      continue;
    }

    size_t consonantCount = rightVowel - leftVowel - 1;
    size_t clusterStart = leftVowel + 1;
    size_t clusterEnd = rightVowel;  // exclusive
    size_t boundary = 0;

    std::u32string cluster(lower.begin() + clusterStart, lower.begin() + clusterEnd);
    std::u32string suffix = lower.substr(clusterEnd);

    // Check for common prefixes and suffixes
    size_t prefixLen = findCommonPrefixLength(lower);
    if (prefixLen > 0 && clusterStart < prefixLen) {
      // If the computed split would fall inside a known prefix, push the boundary
      // to the end of the prefix rather than splitting the prefix itself.
      boundary = prefixLen;
    } else if (isCommonSuffix(lower, clusterEnd)) {
      if (consonantCount == 1 || (lower.size() - clusterEnd) >= 3) {
        boundary = clusterEnd;
      }
    }

    // Avoid splitting within prefixes or suffixes
    if (boundary == 0 && isWithinPrefixOrSuffix(lower, clusterStart)) {
      continue;
    }

    // Avoid very early splits on short openers (re-, pre-, su-, etc.)
    if (boundary == 0 && leftVowel <= 1 && consonantCount == 1) {
      continue;
    }

    // Avoid dubious splits before -age endings
    if (boundary == 0 && suffix == U"age" && consonantCount == 1) {
      continue;
    }

    // Keep very short suffixes together when preceded by inseparable pairs near the start (counter, broken)
    if (boundary == 0 && consonantCount == 2 && isInseparablePair(cluster) && suffix.size() <= 2 && clusterStart <= 3) {
      continue;
    }

    // Morphological suffixes: keep the suffix together when present
    if (boundary == 0 &&
        (suffix == U"es" || suffix == U"ing" || suffix == U"ers" || suffix == U"er" || suffix == U"ly" ||
         suffix == U"ed")) {
      if (suffix == U"es" && wide.size() <= 6) {
        continue;
      }
      if ((suffix == U"ed" || suffix == U"er" || suffix == U"ers") && wide.size() <= 7 && vowelIndices.size() <= 2) {
        continue;
      }
      if (suffix == U"ed" && wide.size() <= 6 && vowelIndices.size() <= 3) {
        continue;
      }
      if (suffix == U"ing" && consonantCount == 2 && cluster[0] == cluster[1]) {
        boundary = clusterStart + 1;  // double consonant + ing -> cut=ting, let=ting
      } else if (suffix == U"es") {
        boundary = clusterEnd;  // prefer keeping -es together (watches, pirates)
      } else if (consonantCount == 1 || suffix == U"ed") {
        boundary = clusterEnd;
      }
    }

    // Special handling for consonant clusters
    if (boundary == 0) {
      if (consonantCount == 1) {
        boundary = clusterStart;  // Prefer keeping the single consonant with the following vowel (side=walk, pri=vate)
        if (boundary == 1 && wide.size() > 3) {
          boundary = 2;
        }
      } else if (consonantCount == 2) {
        // If both consonants are the same (e.g., 'tt'), split between them: rotting -> rot-ting
        if (cluster[0] == cluster[1]) {
          boundary = clusterStart + 1;
        } else if (isInseparablePair(cluster)) {
          // Keep inseparable pairs (ch, sh, th, ck, etc.) attached to the side that keeps balance
          if (cluster == U"ck") {
            boundary = clusterEnd;
          } else if (clusterStart <= 2) {
            boundary = clusterStart + 1;
          } else if (suffix == U"es" || suffix == U"ed") {
            boundary = clusterEnd;
          } else {
            boundary = clusterStart;
          }
        } else {
          // Prefer treating the two-letter cluster as an onset of the following syllable
          // when it's a valid onset (st, tr, pl, etc.). Otherwise split between them.
          if (isAllowedOnset(cluster)) {
            boundary = (clusterStart <= 2) ? clusterStart + 1 : clusterStart;
          } else {
            boundary = clusterStart + 1;
          }
        }
      } else {
        // For longer clusters, prefer keeping a valid onset on the right when possible.
        // Try taking last 3 or last 2 as onset candidates.
        bool placed = false;
        if (consonantCount >= 3) {
          std::u32string last3(lower.begin() + clusterEnd - 3, lower.begin() + clusterEnd);
          if (isAllowedOnset(last3)) {
            boundary = clusterEnd - 3;
            placed = true;
          }
        }
        if (!placed) {
          std::u32string last2(lower.begin() + clusterEnd - 2, lower.begin() + clusterEnd);
          if (isAllowedOnset(last2)) {
            boundary = clusterEnd - 2;
            placed = true;
          }
        }
        if (!placed) {
          boundary = clusterEnd - 1;
        }
      }
    }

    // Avoid producing a one-letter leading syllable (boundary == 1)
    if (boundary == 1 && wide.size() > 2) {
      boundary = 2;
    }

    // Avoid hyphenating before common adverbial suffix
    if (boundary > 0 && suffix == U"ly" && consonantCount >= 2) {
      continue;
    }

    // Avoid splitting common heads (forehead, spearhead)
    if (boundary > 0 && suffix == U"ead") {
      continue;
    }

    // For short two-vowel words with a single bridging consonant, prefer no split
    if (boundary == 0 && vowelIndices.size() == 2 && consonantCount == 1 && wide.size() <= 6) {
      continue;
    }

    if (boundary <= 2 && wide.size() <= 7) {
      continue;
    }

    // Suppress dubious past-tense splits on short double consonant stems
    if (boundary > 0 && suffix == U"ed" && consonantCount == 2 && cluster[0] == cluster[1]) {
      continue;
    }

    if (boundary > 0 && boundary < wide.size()) {
      positions.push_back(boundary);
    }
  }

  // Return character positions (indices into the Unicode codepoint sequence).
  // Tests and consumers expect positions measured in characters, not byte offsets.
  return positions;
}

std::string insertHyphens(const std::string& word, const std::vector<size_t>& positions) {
  std::u32string wide = converter.from_bytes(word);
  std::u32string result;
  result.reserve(wide.size() + positions.size());

  std::unordered_set<size_t> posSet(positions.begin(), positions.end());
  for (size_t i = 0; i < wide.size(); ++i) {
    if (posSet.find(i) != posSet.end()) {
      result.push_back(U'-');
    }
    result.push_back(wide[i]);
  }

  return converter.to_bytes(result);
}

std::vector<size_t> positionsFromAnnotated(const std::string& annotated) {
  std::u32string wide = converter.from_bytes(annotated);
  std::vector<size_t> positions;
  size_t wordIndex = 0;
  for (char32_t c : wide) {
    if (c == U'-') {
      positions.push_back(wordIndex);
    } else {
      wordIndex++;
    }
  }
  return positions;
}

}  // namespace EnglishHyphenation
