#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/core/EInkDisplay.h"
#include "../src/rendering/TextRenderer.h"
#include "../src/resources/fonts/NotoSans26.h"
#include "../src/ui/screens/textview/KnuthPlassLayoutStrategy.h"
#include "../src/ui/screens/textview/StringWordProvider.h"
#include "mocks/WString.h"
#include "mocks/platform_stubs.h"
#include "test_config.h"
#include "test_utils.h"

// Link required implementation units directly for the standalone test build
#include "../src/ui/screens/textview/KnuthPlassLayoutStrategy.cpp"
#include "../src/ui/screens/textview/LayoutStrategy.cpp"
#include "../src/ui/screens/textview/StringWordProvider.cpp"
#include "../src/ui/screens/textview/hyphenation/GermanHyphenation.cpp"
#include "../src/ui/screens/textview/hyphenation/HyphenationStrategy.cpp"
#include "../src/rendering/TextRenderer.cpp"
#include "../src/core/EInkDisplay.cpp"
#include "mocks/platform_stubs.cpp"

struct MergeCheck {
  bool merged = false;
  std::string linePreview;
};

std::vector<std::string> formatLines(const std::vector<LayoutStrategy::Word>& words,
                                     const std::vector<size_t>& breaks) {
  std::vector<std::string> lines;

  size_t lineStart = 0;
  for (size_t breakIdx = 0; breakIdx <= breaks.size(); ++breakIdx) {
    size_t lineEnd = (breakIdx < breaks.size()) ? breaks[breakIdx] : words.size();

    std::ostringstream line;
    for (size_t i = lineStart; i < lineEnd; ++i) {
      if (i > lineStart) {
        line << ' ';
      }
      line << words[i].text.c_str();
    }

    lines.push_back(line.str());
    lineStart = lineEnd;
  }

  return lines;
}

struct ParagraphDiff {
  size_t index;
  std::string text;
  std::vector<std::string> legacyLines;
  std::vector<std::string> fixedLines;
};

std::vector<std::string> loadParagraphs(const std::string& path) {
  std::ifstream in(path);
  std::vector<std::string> paragraphs;
  if (!in.is_open()) {
    std::cerr << "Failed to open sample text: " << path << "\n";
    return paragraphs;
  }

  std::string line;
  std::string current;
  while (std::getline(in, line)) {
    if (line.empty()) {
      if (!current.empty()) {
        paragraphs.push_back(current);
        current.clear();
      }
      continue;
    }

    if (!current.empty()) {
      current.push_back(' ');
    }
    current += line;
  }

  if (!current.empty()) {
    paragraphs.push_back(current);
  }

  return paragraphs;
}

std::vector<LayoutStrategy::Word> collectParagraphWords(KnuthPlassLayoutStrategy& layout, const std::string& text,
                                                        const LayoutStrategy::LayoutConfig& config,
                                                        TextRenderer& renderer) {
  // Measure and cache space width like the real layout
  int16_t bx = 0, by = 0;
  uint16_t bw = 0, bh = 0;
  renderer.getTextBounds(" ", 0, 0, &bx, &by, &bw, &bh);
  layout.setSpaceWidth(static_cast<float>(bw));

  String paragraph(text.c_str());
  StringWordProvider provider(paragraph);

  std::vector<LayoutStrategy::Word> words;
  bool isParagraphEnd = false;
  const int16_t maxWidth = config.pageWidth - config.marginLeft - config.marginRight;

  while (!isParagraphEnd) {
    std::vector<LayoutStrategy::Word> line =
        layout.test_getNextLineDefault(provider, renderer, maxWidth, isParagraphEnd);
    words.insert(words.end(), line.begin(), line.end());

    if (provider.getCurrentIndex() >= paragraph.length()) {
      break;
    }
  }

  return words;
}

MergeCheck findMergedHyphenLine(const std::vector<LayoutStrategy::Word>& words, const std::vector<size_t>& breaks) {
  MergeCheck result;

  size_t lineStart = 0;
  for (size_t breakIdx = 0; breakIdx <= breaks.size(); ++breakIdx) {
    size_t lineEnd = (breakIdx < breaks.size()) ? breaks[breakIdx] : words.size();

    for (size_t i = lineStart; i + 1 < lineEnd; ++i) {
      if (words[i].forceBreakAfter) {
        result.merged = true;

        std::ostringstream preview;
        for (size_t j = lineStart; j < lineEnd; ++j) {
          if (j > lineStart) {
            preview << ' ';
          }
          preview << words[j].text.c_str();
        }
        result.linePreview = preview.str();
        return result;
      }
    }

    lineStart = lineEnd;
  }

  return result;
}

int main() {
  const std::string samplePath = "data/german_hyphenation_sample.txt";
  const std::string diffOutputPath = "test/hyphenation_differences.txt";
  TestUtils::TestRunner runner("Knuth-Plass Hyphenation Page Comparison");

  std::vector<std::string> paragraphs = loadParagraphs(samplePath);
  runner.expectTrue(!paragraphs.empty(), "Sample paragraphs loaded",
                    "No paragraphs loaded from " + samplePath);
  if (paragraphs.empty()) {
    return 1;
  }

  std::ofstream diffOut(diffOutputPath, std::ios::trunc);
  runner.expectTrue(diffOut.is_open(), "Diff output file opened",
                    "Unable to open diff output file: " + diffOutputPath);
  if (!diffOut.is_open()) {
    return 1;
  }

  diffOut << "Knuth-Plass hyphenation differences\n";
  diffOut << "Sample: " << samplePath << "\n\n";

  EInkDisplay display(::TestConfig::DUMMY_PIN, ::TestConfig::DUMMY_PIN, ::TestConfig::DUMMY_PIN,
                      ::TestConfig::DUMMY_PIN, ::TestConfig::DUMMY_PIN, ::TestConfig::DUMMY_PIN);
  TextRenderer renderer(display);
  renderer.setFont(&NotoSans26);

  LayoutStrategy::LayoutConfig config;
  config.marginLeft = ::TestConfig::TEST_MARGIN;
  config.marginRight = ::TestConfig::TEST_MARGIN;
  config.marginTop = ::TestConfig::DEFAULT_MARGIN_TOP;
  config.marginBottom = ::TestConfig::DEFAULT_MARGIN_BOTTOM;
  config.lineHeight = ::TestConfig::DEFAULT_LINE_HEIGHT - 4;  // tighten lines to increase hyphenation
  config.minSpaceWidth = ::TestConfig::DEFAULT_MIN_SPACE_WIDTH - 4;
  config.pageHeight = ::TestConfig::DISPLAY_HEIGHT;
  config.pageWidth = static_cast<int16_t>(::TestConfig::DISPLAY_WIDTH / 2);
  config.alignment = LayoutStrategy::ALIGN_LEFT;
  config.language = Language::GERMAN;

  const size_t maxParagraphs = paragraphs.size();
  bool legacyMergedAny = false;
  bool fixedMergedAny = false;
  bool foundDifference = false;
  std::vector<ParagraphDiff> loggedDiffs;

  for (size_t i = 0; i < maxParagraphs; ++i) {
    const std::string& paragraph = paragraphs[i];

    KnuthPlassLayoutStrategy fixedLayout;
    fixedLayout.setLanguage(Language::GERMAN);
    fixedLayout.setIgnoreForceBreakAfterForTest(false);

    std::vector<LayoutStrategy::Word> fixedWords = collectParagraphWords(fixedLayout, paragraph, config, renderer);
    std::vector<size_t> fixedBreaks =
        fixedLayout.test_calculateBreaks(fixedWords, config.pageWidth - config.marginLeft - config.marginRight);
    MergeCheck fixedMerged = findMergedHyphenLine(fixedWords, fixedBreaks);
    std::vector<std::string> fixedLines = formatLines(fixedWords, fixedBreaks);

    KnuthPlassLayoutStrategy legacyLayout;
    legacyLayout.setLanguage(Language::GERMAN);
    legacyLayout.setIgnoreForceBreakAfterForTest(true);

    std::vector<LayoutStrategy::Word> legacyWords = collectParagraphWords(legacyLayout, paragraph, config, renderer);
    std::vector<size_t> legacyBreaks =
        legacyLayout.test_calculateBreaks(legacyWords, config.pageWidth - config.marginLeft - config.marginRight);
    MergeCheck legacyMerged = findMergedHyphenLine(legacyWords, legacyBreaks);
    std::vector<std::string> legacyLines = formatLines(legacyWords, legacyBreaks);

    legacyMergedAny = legacyMergedAny || legacyMerged.merged;
    fixedMergedAny = fixedMergedAny || fixedMerged.merged;
    bool differs = (legacyLines != fixedLines);
    foundDifference = foundDifference || differs;

    if (differs && loggedDiffs.size() < 2) {
      loggedDiffs.push_back({i + 1, paragraph, legacyLines, fixedLines});
    }
  }

  if (loggedDiffs.empty()) {
    diffOut << "No differing paragraphs found. Extend the sample text or adjust layout parameters.\n";
  } else {
    for (const auto& diff : loggedDiffs) {
      diffOut << "Paragraph " << diff.index << "\n";
      diffOut << "-------------\n";
      diffOut << diff.text << "\n\n";

      diffOut << "Legacy layout:\n";
      for (const auto& line : diff.legacyLines) {
        diffOut << "  " << line << "\n";
      }
      diffOut << "\n";

      diffOut << "Fixed layout:\n";
      for (const auto& line : diff.fixedLines) {
        diffOut << "  " << line << "\n";
      }
      diffOut << "\n";
    }
  }

  runner.expectTrue(legacyMergedAny, "Legacy layout merges hyphen fragments",
                    "Legacy layout never merged hyphenated fragments; extend the sample text or widen the layout search.");
  runner.expectTrue(!fixedMergedAny, "Fixed layout honors forced hyphen breaks",
                    "Fixed layout still merges hyphenated fragments; investigate the forced-break handling.");
  runner.expectTrue(foundDifference, "Fix changes layout decisions",
                    "Layouts matched. Extend sample text or adjust layout parameters to expose the issue.");

  return runner.allPassed() ? 0 : 1;
}
