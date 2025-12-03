#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/core/EInkDisplay.h"
#include "../src/rendering/TextRenderer.h"
#include "../src/resources/fonts/NotoSans26.h"
#include "../src/ui/screens/textview/GreedyLayoutStrategy.h"
#include "../src/ui/screens/textview/KnuthPlassLayoutStrategy.h"
#include "../src/ui/screens/textview/StringWordProvider.h"
#include "mocks/WString.h"
#include "mocks/platform_stubs.h"
#include "test_config.h"
#include "test_utils.h"

// For test build convenience we include some implementation units so the
// single-file test binary links without changing the global build tasks.
#include "../src/ui/screens/textview/GreedyLayoutStrategy.cpp"
#include "../src/ui/screens/textview/KnuthPlassLayoutStrategy.cpp"
#include "../src/ui/screens/textview/StringWordProvider.cpp"

struct TestRun {
  std::string name;
  bool useGreedyLayout;
  bool incrementalMode;
  bool disableRendering;
  int maxPages;
};

void runTestConfiguration(const TestRun& testRun, TestUtils::TestRunner& runner, EInkDisplay& display,
                          TextRenderer& renderer, const String& fullText) {
  std::cout << "\n=== Running: " << testRun.name << " ===\n";

  auto savePage = [&](int pageIndex, String postfix) {
    std::ostringstream ss;
    ss << ::TestConfig::TEST_OUTPUT_DIR << "/output_" << std::setw(3) << std::setfill('0') << pageIndex
       << std::setfill(' ') << postfix.c_str() << ".pbm";
    std::string out = ss.str();
    display.saveFrameBufferAsPBM(out.c_str());
  };

  StringWordProvider provider(fullText);

  // Create appropriate layout strategy
  LayoutStrategy* layout = nullptr;
  KnuthPlassLayoutStrategy* knuthPlassLayout = nullptr;
  if (testRun.useGreedyLayout) {
    layout = new GreedyLayoutStrategy();
  } else {
    knuthPlassLayout = new KnuthPlassLayoutStrategy();
    layout = knuthPlassLayout;
  }
  LayoutStrategy::LayoutConfig layoutConfig;
  layoutConfig.marginLeft = ::TestConfig::DEFAULT_MARGIN_LEFT;
  layoutConfig.marginRight = ::TestConfig::DEFAULT_MARGIN_RIGHT;
  layoutConfig.marginTop = ::TestConfig::DEFAULT_MARGIN_TOP;
  layoutConfig.marginBottom = ::TestConfig::DEFAULT_MARGIN_BOTTOM;
  layoutConfig.lineHeight = ::TestConfig::DEFAULT_LINE_HEIGHT;
  layoutConfig.minSpaceWidth = ::TestConfig::DEFAULT_MIN_SPACE_WIDTH;
  layoutConfig.pageWidth = ::TestConfig::DISPLAY_WIDTH;
  layoutConfig.pageHeight = ::TestConfig::DISPLAY_HEIGHT;
  layoutConfig.alignment = LayoutStrategy::ALIGN_LEFT;

  // Traverse the entire document forward, and immediately check backward navigation
  std::vector<std::pair<int, int>> pageRanges;  // pair<start, end>
  int pageStart = 249636;
  int pageIndex = 0;

  // // move towards the first occurence of the word "vorzuziehen" for initial debugging
  // while (provider.hasNextWord()) {
  //   String word = provider.getNextWord();
  //   if (word == String("vorzuziehen")) {
  //     pageStart = provider.getCurrentIndex() - word.length();
  //     provider.setPosition(pageStart);
  //     break;
  //   }
  // }

  while (true) {
    if (!testRun.disableRendering) {
      display.clearScreen(0xFF);
    }

    provider.setPosition(pageStart);

    // Reset mismatch flag before layout
    if (knuthPlassLayout) {
      knuthPlassLayout->resetLineCountMismatch();
    }

    int endPos = layout->layoutText(provider, renderer, layoutConfig, testRun.disableRendering);

    // Check for line count mismatch in KnuthPlass layout
    if (knuthPlassLayout && knuthPlassLayout->hasLineCountMismatch()) {
      std::string errorMsg = testRun.name + " - Page " + std::to_string(pageIndex) +
                             " line count mismatch - Expected " +
                             std::to_string(knuthPlassLayout->getExpectedLineCount()) + " lines, got " +
                             std::to_string(knuthPlassLayout->getActualLineCount());
      std::cerr << errorMsg << "\n";
      runner.expectTrue(false, testRun.name + " - Line count check from position " + std::to_string(pageStart),
                        errorMsg);
    }

    // record the start and end positions for this page
    pageRanges.push_back(std::make_pair(pageStart, endPos));

    if (!testRun.disableRendering) {
      savePage(pageIndex, "_0");
    }

    // test backward navigation from current page
    if (provider.getPercentage(endPos) < 1.0f) {
      int expectedPrevStart = pageRanges[pageIndex].first;
      int expectedPrevEnd = pageRanges[pageIndex].second;

      int computedPrevStart = layout->getPreviousPageStart(provider, renderer, layoutConfig, endPos);

      // Render the computed previous page to determine its end position
      if (!testRun.disableRendering) {
        display.clearScreen(0xFF);
      }
      provider.setPosition(computedPrevStart);
      int computedPrevEnd = layout->layoutText(provider, renderer, layoutConfig, testRun.disableRendering);

      bool startMatch = (computedPrevStart == expectedPrevStart);
      bool endMatch = (computedPrevEnd == expectedPrevEnd);

      if (!startMatch || !endMatch) {
        std::string errorMsg = testRun.name + " - Page " + std::to_string(pageIndex) +
                               " backward check - computedPrevStart=" + std::to_string(computedPrevStart) +
                               " expectedPrevStart=" + std::to_string(expectedPrevStart) +
                               ", computedPrevEnd=" + std::to_string(computedPrevEnd) +
                               " expectedPrevEnd=" + std::to_string(expectedPrevEnd);
        std::cerr << errorMsg << "\n";
        runner.expectTrue(false, testRun.name + " - Backward navigation from page " + std::to_string(pageIndex),
                          errorMsg);
      }
    }

    // Stop if we've reached the end of the provider
    if (provider.getPercentage(endPos) >= 1.0f) {
      ++pageIndex;
      break;
    }

    // Safety: if no progress, break to avoid infinite loop
    if (endPos <= pageStart) {
      std::cerr << "No progress while laying out page " << pageIndex << ", stopping traversal.\n";
      ++pageIndex;
      break;
    }

    // Safety: limit pages
    if (pageIndex + 1 >= testRun.maxPages) {
      std::cerr << "Reached max page limit (" << testRun.maxPages << "), stopping.\n";
      ++pageIndex;
      break;
    }

    if (testRun.incrementalMode) {
      // Move one word forward from the start of the current page
      provider.setPosition(pageStart);
      layout->test_getNextLineDefault(
          provider, renderer, layoutConfig.pageWidth - layoutConfig.marginLeft - layoutConfig.marginRight, *(new bool));
      int nextPos = provider.getCurrentIndex();

      // If we can't move forward, we're done
      if (nextPos <= pageStart) {
        std::cerr << "Cannot advance one word from position " << pageStart << ", stopping.\n";
        ++pageIndex;
        break;
      }
      pageStart = nextPos;
    } else {
      // Normal mode: jump to the end of the current page
      pageStart = endPos;
    }
    ++pageIndex;
  }

  std::cout << "Forward traversal produced " << pageRanges.size() << " pages.\n";

  delete layout;
}

int main() {
  TestUtils::TestRunner runner("TextLayout Page Render Test");

  // Create display with dummy pins
  EInkDisplay display(::TestConfig::DUMMY_PIN, ::TestConfig::DUMMY_PIN, ::TestConfig::DUMMY_PIN,
                      ::TestConfig::DUMMY_PIN, ::TestConfig::DUMMY_PIN, ::TestConfig::DUMMY_PIN);

  // Initialize (no-op for many functions in desktop stubs)
  display.begin();

  // Clear to white (0xFF is white in driver)
  display.clearScreen(0xFF);

  // Render some text onto the frame buffer using the TextRenderer
  TextRenderer renderer(display);
  // Use our local font
  renderer.setFont(&NotoSans26);
  renderer.setTextColor(TextRenderer::COLOR_BLACK);

  const std::string filepath = ::TestConfig::DEFAULT_TEST_FILE;
  std::ifstream infile(filepath);
  if (!infile) {
    std::cerr << "Failed to open '" << filepath << "'\n";
    return 1;
  }

  // Ensure output directory exists
  std::filesystem::create_directories(::TestConfig::TEST_OUTPUT_DIR);

  // Read entire file into memory
  std::string content((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
  String fullText(content.c_str());

  // Define test configurations
  std::vector<TestRun> testConfigs = {
      // {"Greedy - Incremental All Pages - No Render", true,  true,  true,  99999},
      // {"Greedy - Normal - No Render",                true,  false, true,  99999},
      {"KnuthPlass - Incremental - No Render", false, true,  true,  99999},
      {"KnuthPlass - Normal - No Render",      false, false, true,  99999},
      {"KnuthPlass - Normal - With Render",    false, false, false, 99999},
      // {"KnuthPlass - Normal - With Render", false, false, false, 1},
  };

  // Run all test configurations
  for (const auto& config : testConfigs) {
    runTestConfiguration(config, runner, display, renderer, fullText);
  }

  return runner.allPassed() ? 0 : 1;
}
