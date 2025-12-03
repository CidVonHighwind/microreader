#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "../src/core/EInkDisplay.h"
#include "../src/rendering/TextRenderer.h"
#include "../src/resources/fonts/NotoSans26.h"
#include "../src/ui/screens/textview/GreedyLayoutStrategy.h"
#include "../src/ui/screens/textview/StringWordProvider.h"
#include "test_config.h"
#include "test_utils.h"

static std::string joinLine(const std::vector<LayoutStrategy::Word>& line) {
  std::string out;
  for (size_t i = 0; i < line.size(); ++i) {
    out += line[i].text.c_str();
    if (i + 1 < line.size())
      out += ' ';
  }
  return out;
}

int main(int argc, char** argv) {
  TestUtils::TestRunner runner("GreedyLayout Bidirectional Paragraph Test");

  std::string path;
  if (argc >= 2)
    path = argv[1];
  else
    path = TestConfig::DEFAULT_TEST_FILE;

  std::string content = TestUtils::readFile(path);
  if (content.empty()) {
    std::cerr << "Failed to open '" << path << "'\n";
    return 2;
  }

  // Create two providers for forward and backward scanning
  String s(content.c_str());
  StringWordProvider provider(s);
  StringWordProvider providerB(s);

  // Create display + renderer like other host tests so we can use the real TextRenderer
  EInkDisplay display(TestConfig::DUMMY_PIN, TestConfig::DUMMY_PIN, TestConfig::DUMMY_PIN, TestConfig::DUMMY_PIN,
                      TestConfig::DUMMY_PIN, TestConfig::DUMMY_PIN);
  display.begin();
  TextRenderer renderer(display);
  renderer.setFont(&NotoSans26);

  GreedyLayoutStrategy layout;

  // Initialize layout (sets internal space width). layoutText will reset provider position.
  LayoutStrategy::LayoutConfig cfg;
  cfg.marginLeft = 0;
  cfg.marginRight = 0;
  cfg.marginTop = 0;
  cfg.marginBottom = 0;
  cfg.lineHeight = 10;
  cfg.minSpaceWidth = 1;
  // pageWidth determines maxWidth used by getNextLine/getPrevLine. Choose a value that will create multiple lines.
  cfg.pageWidth = TestConfig::DISPLAY_WIDTH;
  cfg.pageHeight = TestConfig::DISPLAY_HEIGHT;
  cfg.alignment = LayoutStrategy::ALIGN_LEFT;

  layout.layoutText(provider, renderer, cfg);

  const int16_t maxWidth = static_cast<int16_t>(cfg.pageWidth - cfg.marginLeft - cfg.marginRight);

  struct LineInfo {
    std::string text;
    int startPos;
    int endPos;
    bool isParagraphEnd;
  };

  int paragraphNum = 0;
  int totalLines = 0;

  // Process each paragraph: forward pass, backward pass, compare, repeat
  while (provider.hasNextWord()) {
    paragraphNum++;

    // Forward pass: collect lines until paragraph end
    std::vector<LineInfo> forwardLines;
    int paragraphStartPos = provider.getCurrentIndex();

    bool reachedParagraphEnd = false;
    while (provider.hasNextWord() && !reachedParagraphEnd) {
      bool isParagraphEnd = false;
      int startPos = provider.getCurrentIndex();
      auto line = layout.test_getNextLine(provider, renderer, maxWidth, isParagraphEnd);
      int endPos = provider.getCurrentIndex();

      std::string lineText = joinLine(line);
      forwardLines.push_back({lineText, startPos, endPos, isParagraphEnd});

      reachedParagraphEnd = isParagraphEnd;
    }

    int paragraphEndPos = provider.getCurrentIndex();

    std::vector<LineInfo> backwardLines;
    bool hitParagraphStart = false;
    while (provider.getCurrentIndex() > 0) {
      bool isParagraphEnd = false;
      int endPos = provider.getCurrentIndex();
      auto line = layout.test_getPrevLine(provider, renderer, maxWidth, isParagraphEnd);
      int startPos = provider.getCurrentIndex();

      std::string lineText = joinLine(line);
      backwardLines.insert(backwardLines.begin(), {lineText, startPos, endPos, isParagraphEnd});

      // If we hit the paragraph start (encountered the previous paragraph's ending newline), stop here
      if (isParagraphEnd) {
        hitParagraphStart = true;
        // Position is now at the newline, advance past it to get paragraph start
        provider.setPosition(provider.getCurrentIndex());
        break;
      }

      // Also stop if we've gone back past the paragraph start (shouldn't happen with correct logic)
      if (provider.getCurrentIndex() <= paragraphStartPos) {
        break;
      }
    }

    int backwardStartPos = provider.getCurrentIndex();

    // Compare forward and backward passes for this paragraph
    // The line breaks don't need to match, but the paragraph start/end positions should
    std::string errorMsg;
    bool hasParagraphError = false;

    // Check that both passes cover the same text range
    if (paragraphStartPos != backwardStartPos) {
      hasParagraphError = true;
      errorMsg = "Paragraph " + std::to_string(paragraphNum) +
                 " start position mismatch! Forward: " + std::to_string(paragraphStartPos) +
                 ", Backward: " + std::to_string(backwardStartPos);
      runner.expectTrue(false, "Paragraph start position match", errorMsg);
    } else {
      runner.expectTrue(true, "Paragraph " + std::to_string(paragraphNum) + " start position match", "", true);
    }

    // Both should have same paragraph end
    if (forwardLines.empty() != backwardLines.empty()) {
      hasParagraphError = true;
      runner.expectTrue(false, "Paragraph " + std::to_string(paragraphNum) + " line consistency",
                        "One pass has lines, the other doesn't!");
    } else if (!forwardLines.empty() && !backwardLines.empty()) {
      // Check that the last line in forward pass has isParagraphEnd matching first line in backward
      bool forwardHasParagraphEnd = forwardLines.back().isParagraphEnd;
      bool backwardHasParagraphEnd = backwardLines.front().isParagraphEnd;

      // Special cases where flags might legitimately differ:
      // 1. Last paragraph: Forward won't see a newline at EOF
      // 2. First paragraph: Backward hits beginning of file (position 0) instead of a newline
      bool isLastParagraph = (paragraphEndPos >= static_cast<int>(content.length()) - 1) || !provider.hasNextWord();
      bool isFirstParagraph = (paragraphStartPos == 0);

      if (forwardHasParagraphEnd != backwardHasParagraphEnd && !isLastParagraph && !isFirstParagraph) {
        hasParagraphError = true;
        errorMsg = "Paragraph end flag mismatch! Forward last line: " + std::to_string(forwardHasParagraphEnd) +
                   ", Backward first line: " + std::to_string(backwardHasParagraphEnd);
        runner.expectTrue(false, "Paragraph " + std::to_string(paragraphNum) + " end flag match", errorMsg);
      } else {
        runner.expectTrue(true, "Paragraph " + std::to_string(paragraphNum) + " end flag match", "", true);
      }
    }

    // Print detailed debug info only for failing paragraphs
    if (hasParagraphError) {
      std::cerr << "\n=== Paragraph " << paragraphNum << " FAILED ===\n";
      std::cerr << "Forward start: " << paragraphStartPos << ", end: " << paragraphEndPos << "\n";
      std::cerr << "Backward start: " << backwardStartPos << ", end: " << paragraphEndPos << "\n";
      std::cerr << "Forward lines (" << forwardLines.size() << "):\n";
      for (size_t i = 0; i < forwardLines.size(); i++) {
        std::cerr << "  [" << i << "] pos " << forwardLines[i].startPos << "-" << forwardLines[i].endPos
                  << " isParagraphEnd=" << forwardLines[i].isParagraphEnd << ": \"" << forwardLines[i].text << "\"\n";
      }
      std::cerr << "Backward lines (" << backwardLines.size() << "):\n";
      for (size_t i = 0; i < backwardLines.size(); i++) {
        std::cerr << "  [" << i << "] pos " << backwardLines[i].startPos << "-" << backwardLines[i].endPos
                  << " isParagraphEnd=" << backwardLines[i].isParagraphEnd << ": \"" << backwardLines[i].text << "\"\n";
      }
      std::cerr << "===========================\n\n";
    }

    totalLines += forwardLines.size();

    // Move provider back to end of paragraph for next forward pass
    provider.setPosition(paragraphEndPos);
  }

  std::cout << "\nProcessed " << paragraphNum << " paragraphs with " << totalLines << " total lines\n";

  return runner.allPassed() ? 0 : 1;
}
