/**
 * GreedyLayoutBidirectionalParagraphTest.cpp - Bidirectional Paragraph Layout Test
 *
 * Tests that forward and backward line scanning produce consistent results.
 * The provider and layout to test are configured in test_globals.h.
 */

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "core/EInkDisplay.h"
#include "rendering/TextRenderer.h"
#include "resources/fonts/FontDefinitions.h"
#include "test_config.h"
#include "test_globals.h"
#include "test_utils.h"

static std::string joinLine(const LayoutStrategy::Line& line) {
  std::string out;
  for (size_t i = 0; i < line.words.size(); ++i) {
    out += line.words[i].text.c_str();
    if (i + 1 < line.words.size())
      out += ' ';
  }
  return out;
}

static std::string getFirstWord(const LayoutStrategy::Line& line) {
  // Skip leading whitespace/empty words
  for (const auto& word : line.words) {
    std::string w = word.text.c_str();
    // Skip if empty or just whitespace
    if (!w.empty() && w.find_first_not_of(" \t\n\r") != std::string::npos) {
      return w;
    }
  }
  return "";
}

static std::string getLastWord(const LayoutStrategy::Line& line) {
  // Skip trailing whitespace/empty words
  for (auto it = line.words.rbegin(); it != line.words.rend(); ++it) {
    std::string w = it->text.c_str();
    // Skip if empty or just whitespace
    if (!w.empty() && w.find_first_not_of(" \t\n\r") != std::string::npos) {
      return w;
    }
  }
  return "";
}

void runBidirectionalParagraphTest(TestUtils::TestRunner& runner, EInkDisplay& display, TextRenderer& renderer) {
  std::cout << "\n=== Running Bidirectional Paragraph Test ===\n";
  std::cout << "Provider: " << TestGlobals::getProviderName() << "\n";
  std::cout << "Layout: " << TestGlobals::getLayoutName() << "\n";

  // Get the global provider and layout
  TestGlobals::resetProvider();
  WordProvider& provider = TestGlobals::provider();
  LayoutStrategy& layout = TestGlobals::layout();

  // Initialize layout config
  LayoutStrategy::LayoutConfig cfg;
  cfg.marginLeft = 0;
  cfg.marginRight = 0;
  cfg.marginTop = 0;
  cfg.marginBottom = 0;
  cfg.lineHeight = 10;
  cfg.minSpaceWidth = 1;
  cfg.pageWidth = TestConfig::DISPLAY_WIDTH;
  cfg.pageHeight = TestConfig::DISPLAY_HEIGHT;
  cfg.alignment = LayoutStrategy::ALIGN_LEFT;

  // Initialize layout (sets internal space width)
  layout.layoutText(provider, renderer, cfg);

  const int16_t maxWidth = static_cast<int16_t>(cfg.pageWidth - cfg.marginLeft - cfg.marginRight);

  struct LineInfo {
    std::string text;
    std::string firstWord;
    std::string lastWord;
    int startPos;
    int endPos;
    bool isParagraphEnd;
  };

  struct ParagraphInfo {
    std::vector<LineInfo> lines;
    int startPos;
    int endPos;
  };

  // Debug: dump last 30 words from provider to see what's being returned
  std::cout << "\n=== Debug: Last 30 words from provider ===\n";
  provider.reset();
  // Skip to near the end (closer to position 10300)
  while (provider.hasNextWord()) {
    int pos = provider.getCurrentIndex();
    if (pos > 10280)
      break;  // Get close to the end
    provider.getNextWord();
  }
  // Now print the remaining words until end
  for (int i = 0; i < 30 && provider.hasNextWord(); i++) {
    int pos = provider.getCurrentIndex();
    String word = provider.getNextWord().text;
    int posAfter = provider.getCurrentIndex();
    std::string escaped;
    for (size_t j = 0; j < word.length(); j++) {
      char c = word.charAt(j);
      if (c == '\n')
        escaped += "\\n";
      else if (c == '\r')
        escaped += "\\r";
      else if (c == '\t')
        escaped += "\\t";
      else if (c == ' ')
        escaped += "_";
      else if (c < 32 || c > 126)
        escaped += "[" + std::to_string((int)(unsigned char)c) + "]";
      else
        escaped += c;
    }
    std::cout << "  [" << i << "] pos " << pos << "->" << posAfter << " len=" << word.length() << ": \"" << escaped
              << "\"\n";
  }
  std::cout << "Final position: " << provider.getCurrentIndex() << "\n";
  std::cout << "hasNextWord after dump: " << (provider.hasNextWord() ? "true" : "false") << "\n";
  std::cout << "=== End Debug ===\n\n";

  // Reset provider for actual test
  provider.reset();

  // Forward pass: collect all paragraphs with their lines
  std::vector<ParagraphInfo> forwardParagraphs;

  while (provider.hasNextWord()) {
    ParagraphInfo para;
    para.startPos = provider.getCurrentIndex();

    bool reachedParagraphEnd = false;
    while (provider.hasNextWord() && !reachedParagraphEnd) {
      bool isParagraphEnd = false;
      int startPos = provider.getCurrentIndex();
      auto line = layout.test_getNextLineDefault(provider, renderer, maxWidth, isParagraphEnd);
      int endPos = provider.getCurrentIndex();

      // Skip empty lines at end of content (not intentional empty lines)
      if (line.words.empty() && !isParagraphEnd) {
        break;
      }

      LineInfo lineInfo;
      lineInfo.text = joinLine(line);
      lineInfo.firstWord = getFirstWord(line);
      lineInfo.lastWord = getLastWord(line);
      lineInfo.startPos = startPos;
      lineInfo.endPos = endPos;
      lineInfo.isParagraphEnd = isParagraphEnd;
      para.lines.push_back(lineInfo);

      reachedParagraphEnd = isParagraphEnd;
    }

    para.endPos = provider.getCurrentIndex();
    // Only add non-empty paragraphs
    if (!para.lines.empty()) {
      forwardParagraphs.push_back(para);
    }
  }

  std::cout << "Forward pass collected " << forwardParagraphs.size() << " paragraphs\n";

  // Backward pass: for each paragraph, go backward from its end and collect lines
  int errors = 0;
  for (size_t p = 0; p < forwardParagraphs.size(); p++) {
    const ParagraphInfo& fwdPara = forwardParagraphs[p];

    // Position at end of this paragraph
    provider.setPosition(fwdPara.endPos);

    // Collect lines going backward until we hit a paragraph end (or start of content)
    std::vector<LineInfo> backwardLines;
    while (provider.getCurrentIndex() > fwdPara.startPos) {
      bool isParagraphEnd = false;
      int endPos = provider.getCurrentIndex();
      auto line = layout.test_getPrevLine(provider, renderer, maxWidth, isParagraphEnd);
      int startPos = provider.getCurrentIndex();

      LineInfo lineInfo;
      lineInfo.text = joinLine(line);
      lineInfo.firstWord = getFirstWord(line);
      lineInfo.lastWord = getLastWord(line);
      lineInfo.startPos = startPos;
      lineInfo.endPos = endPos;
      lineInfo.isParagraphEnd = isParagraphEnd;
      backwardLines.insert(backwardLines.begin(), lineInfo);

      // Stop if we've reached the paragraph start
      if (startPos <= fwdPara.startPos) {
        break;
      }
    }

    // Compare: check line count, first word of first line, last word of last line
    bool lineCountMatch = (fwdPara.lines.size() == backwardLines.size());

    std::string fwdFirstWord = fwdPara.lines.empty() ? "" : fwdPara.lines.front().firstWord;
    std::string bwdFirstWord = backwardLines.empty() ? "" : backwardLines.front().firstWord;
    bool firstWordMatch = (fwdFirstWord == bwdFirstWord);

    std::string fwdLastWord = fwdPara.lines.empty() ? "" : fwdPara.lines.back().lastWord;
    std::string bwdLastWord = backwardLines.empty() ? "" : backwardLines.back().lastWord;
    bool lastWordMatch = (fwdLastWord == bwdLastWord);

    bool hasParagraphError = !lineCountMatch || !firstWordMatch || !lastWordMatch;

    if (hasParagraphError) {
      errors++;
      std::cerr << "\n=== Paragraph " << p << " FAILED ===\n";
      std::cerr << "Forward: " << fwdPara.lines.size() << " lines, Backward: " << backwardLines.size() << " lines\n";
      std::cerr << "First word - Forward: \"" << fwdFirstWord << "\", Backward: \"" << bwdFirstWord << "\"\n";
      std::cerr << "Last word - Forward: \"" << fwdLastWord << "\", Backward: \"" << bwdLastWord << "\"\n";

      std::cerr << "Forward lines:\n";
      for (size_t i = 0; i < fwdPara.lines.size(); i++) {
        std::cerr << "  [" << i << "] pos " << fwdPara.lines[i].startPos << "-" << fwdPara.lines[i].endPos << ": \""
                  << fwdPara.lines[i].text << "\"\n";
      }
      std::cerr << "Backward lines:\n";
      for (size_t i = 0; i < backwardLines.size(); i++) {
        std::cerr << "  [" << i << "] pos " << backwardLines[i].startPos << "-" << backwardLines[i].endPos << ": \""
                  << backwardLines[i].text << "\"\n";
      }
      std::cerr << "===========================\n";
    }

    runner.expectTrue(
        lineCountMatch, "Paragraph " + std::to_string(p) + " line count match",
        "Forward: " + std::to_string(fwdPara.lines.size()) + ", Backward: " + std::to_string(backwardLines.size()),
        lineCountMatch);  // silent if passing
    runner.expectTrue(firstWordMatch, "Paragraph " + std::to_string(p) + " first word match",
                      "Forward: \"" + fwdFirstWord + "\", Backward: \"" + bwdFirstWord + "\"", firstWordMatch);
    runner.expectTrue(lastWordMatch, "Paragraph " + std::to_string(p) + " last word match",
                      "Forward: \"" + fwdLastWord + "\", Backward: \"" + bwdLastWord + "\"", lastWordMatch);
  }

  std::cout << "Backward verification: " << errors << " errors out of " << forwardParagraphs.size() << " paragraphs\n";

  // Print first and last few lines from all paragraphs
  std::vector<LineInfo> allForwardLines;
  for (const auto& para : forwardParagraphs) {
    for (const auto& line : para.lines) {
      allForwardLines.push_back(line);
    }
  }

  const size_t numLinesToShow = 5;
  std::cout << "\n--- First " << std::min(numLinesToShow, allForwardLines.size()) << " forward lines ---\n";
  for (size_t i = 0; i < std::min(numLinesToShow, allForwardLines.size()); i++) {
    std::cout << "  [" << i << "] pos " << allForwardLines[i].startPos << "-" << allForwardLines[i].endPos
              << " isParagraphEnd=" << allForwardLines[i].isParagraphEnd << ": \"" << allForwardLines[i].text << "\"\n";
  }

  if (allForwardLines.size() > numLinesToShow * 2) {
    std::cout << "  ... (" << (allForwardLines.size() - numLinesToShow * 2) << " lines omitted) ...\n";
  }

  std::cout << "\n--- Last " << std::min(numLinesToShow, allForwardLines.size()) << " forward lines ---\n";
  size_t startIdx = allForwardLines.size() > numLinesToShow ? allForwardLines.size() - numLinesToShow : 0;
  for (size_t i = startIdx; i < allForwardLines.size(); i++) {
    std::cout << "  [" << i << "] pos " << allForwardLines[i].startPos << "-" << allForwardLines[i].endPos
              << " isParagraphEnd=" << allForwardLines[i].isParagraphEnd << ": \"" << allForwardLines[i].text << "\"\n";
  }

  // Now do a full backward pass from end to start and print first/last 5 lines
  std::cout << "\n=== Full backward pass ===\n";
  int endPosition = forwardParagraphs.empty() ? 0 : forwardParagraphs.back().endPos;
  provider.setPosition(endPosition);
  std::vector<LineInfo> allBackwardLines;
  while (provider.getCurrentIndex() > 0) {
    bool isParagraphEnd = false;
    int endPos = provider.getCurrentIndex();
    auto line = layout.test_getPrevLine(provider, renderer, maxWidth, isParagraphEnd);
    int startPos = provider.getCurrentIndex();

    LineInfo lineInfo;
    lineInfo.text = joinLine(line);
    lineInfo.firstWord = getFirstWord(line);
    lineInfo.lastWord = getLastWord(line);
    lineInfo.startPos = startPos;
    lineInfo.endPos = endPos;
    lineInfo.isParagraphEnd = isParagraphEnd;
    allBackwardLines.insert(allBackwardLines.begin(), lineInfo);

    if (startPos <= 0)
      break;
  }

  std::cout << "Backward pass collected " << allBackwardLines.size() << " lines total\n";

  std::cout << "\n--- First " << std::min(numLinesToShow, allBackwardLines.size()) << " backward lines ---\n";
  for (size_t i = 0; i < std::min(numLinesToShow, allBackwardLines.size()); i++) {
    std::cout << "  [" << i << "] pos " << allBackwardLines[i].startPos << "-" << allBackwardLines[i].endPos
              << " isParagraphEnd=" << allBackwardLines[i].isParagraphEnd << ": \"" << allBackwardLines[i].text
              << "\"\n";
  }

  if (allBackwardLines.size() > numLinesToShow * 2) {
    std::cout << "  ... (" << (allBackwardLines.size() - numLinesToShow * 2) << " lines omitted) ...\n";
  }

  std::cout << "\n--- Last " << std::min(numLinesToShow, allBackwardLines.size()) << " backward lines ---\n";
  size_t bwdStartIdx = allBackwardLines.size() > numLinesToShow ? allBackwardLines.size() - numLinesToShow : 0;
  for (size_t i = bwdStartIdx; i < allBackwardLines.size(); i++) {
    std::cout << "  [" << i << "] pos " << allBackwardLines[i].startPos << "-" << allBackwardLines[i].endPos
              << " isParagraphEnd=" << allBackwardLines[i].isParagraphEnd << ": \"" << allBackwardLines[i].text
              << "\"\n";
  }
}

int main(int argc, char** argv) {
  TestUtils::TestRunner runner("Layout Bidirectional Paragraph Test");

  // Initialize global provider and layout from test_globals.h configuration
  if (!TestGlobals::init()) {
    std::cerr << "Failed to initialize test globals\n";
    return 2;
  }

  // Create display with dummy pins
  EInkDisplay display(TestConfig::DUMMY_PIN, TestConfig::DUMMY_PIN, TestConfig::DUMMY_PIN, TestConfig::DUMMY_PIN,
                      TestConfig::DUMMY_PIN, TestConfig::DUMMY_PIN);
  display.begin();

  // Create renderer
  TextRenderer renderer(display);
  renderer.setFontFamily(&bookerlyFamily);

  // Run the bidirectional test
  runBidirectionalParagraphTest(runner, display, renderer);

  // Cleanup
  TestGlobals::cleanup();

  return runner.allPassed() ? 0 : 1;
}
