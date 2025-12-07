/**
 * TextLayoutPageRenderTest.cpp - Text Layout and Page Rendering Test
 *
 * Tests page layout and backward navigation functionality.
 * The provider and layout to test are configured in test_globals.h.
 */

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "WString.h"
#include "core/EInkDisplay.h"
#include "platform_stubs.h"
#include "rendering/TextRenderer.h"
#include "resources/fonts/NotoSans26.h"
#include "test_config.h"
#include "test_globals.h"
#include "text/hyphenation/HyphenationStrategy.h"

// Test configuration structure
struct PageTestConfig {
  std::string name;
  bool incrementalMode = false;
  bool disableRendering = true;
  int pageStart = 0;
  int maxPages = 5000;

  std::string getFullName() const {
    std::string providerName = TestGlobals::getProviderName();
    std::string layoutName = TestGlobals::getLayoutName();
    return name + " [" + providerName + " + " + layoutName + "]";
  }
};

void runTestConfiguration(const PageTestConfig& testConfig, TestUtils::TestRunner& runner, EInkDisplay& display,
                          TextRenderer& renderer) {
  std::cout << "\n=== Running: " << testConfig.getFullName() << " ===\n";

  auto savePage = [&](int pageIndex, String postfix) {
    std::ostringstream ss;
    ss << ::TestConfig::TEST_OUTPUT_DIR << "/output_" << std::setw(3) << std::setfill('0') << pageIndex
       << std::setfill(' ') << postfix.c_str() << ".pbm";
    std::string out = ss.str();
    display.saveFrameBufferAsPBM(out.c_str());
  };

  // Get the global provider and layout
  TestGlobals::resetProvider();
  WordProvider& provider = TestGlobals::provider();
  LayoutStrategy& layout = TestGlobals::layout();

  // Check if it's KnuthPlass for line count mismatch checking
  KnuthPlassLayoutStrategy* knuthPlassLayout = dynamic_cast<KnuthPlassLayoutStrategy*>(&layout);

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
  layoutConfig.language = Language::GERMAN;

  // Set the language on the layout strategy
  layout.setLanguage(layoutConfig.language);

  // Traverse the entire document forward, and immediately check backward navigation
  std::vector<std::pair<int, int>> pageRanges;  // pair<start, end>
  int pageStart = testConfig.pageStart;
  int pageIndex = 0;

  while (true) {
    if (!testConfig.disableRendering) {
      display.clearScreen(0xFF);
    }

    provider.setPosition(pageStart);

    // Reset mismatch flag before layout
    if (knuthPlassLayout) {
      knuthPlassLayout->resetLineCountMismatch();
    }

    LayoutStrategy::PageLayout pageLayout = layout.layoutText(provider, renderer, layoutConfig);
    int endPos = pageLayout.endPosition;

    if (!testConfig.disableRendering) {
      layout.renderPage(pageLayout, renderer, layoutConfig);
    }

    // Check for line count mismatch in KnuthPlass layout
    if (knuthPlassLayout) {
      if (knuthPlassLayout->hasLineCountMismatch()) {
        std::string errorMsg = testConfig.name + " - Page " + std::to_string(pageIndex) +
                               " line count mismatch - Expected " +
                               std::to_string(knuthPlassLayout->getExpectedLineCount()) + " lines, got " +
                               std::to_string(knuthPlassLayout->getActualLineCount());
        std::cerr << errorMsg << "\n";
        runner.expectTrue(false, testConfig.name + " - Line count check page " + std::to_string(pageIndex), errorMsg);
      } else {
        runner.expectTrue(true, testConfig.name + " - Line count check page " + std::to_string(pageIndex), "",
                          true);  // silent=true
      }
    }

    // record the start and end positions for this page
    pageRanges.push_back(std::make_pair(pageStart, endPos));

    if (!testConfig.disableRendering) {
      savePage(pageIndex, "_0");
    }

    // test backward navigation from current page
    if (provider.getChapterPercentage(endPos) < 1.0f) {
      int expectedPrevStart = pageRanges[pageIndex].first;
      int expectedPrevEnd = pageRanges[pageIndex].second;

      int computedPrevStart = layout.getPreviousPageStart(provider, renderer, layoutConfig, endPos);

      // Render the computed previous page to determine its end position
      if (!testConfig.disableRendering) {
        display.clearScreen(0xFF);
      }
      provider.setPosition(computedPrevStart);
      LayoutStrategy::PageLayout prevLayout = layout.layoutText(provider, renderer, layoutConfig);
      int computedPrevEnd = prevLayout.endPosition;

      if (!testConfig.disableRendering) {
        layout.renderPage(prevLayout, renderer, layoutConfig);
      }

      bool startMatch = (computedPrevStart == expectedPrevStart);
      bool endMatch = (computedPrevEnd == expectedPrevEnd);

      if (!startMatch || !endMatch) {
        std::string errorMsg = testConfig.name + " - Page " + std::to_string(pageIndex) +
                               " backward check - computedPrevStart=" + std::to_string(computedPrevStart) +
                               " expectedPrevStart=" + std::to_string(expectedPrevStart) +
                               ", computedPrevEnd=" + std::to_string(computedPrevEnd) +
                               " expectedPrevEnd=" + std::to_string(expectedPrevEnd);
        std::cerr << errorMsg << "\n";
        runner.expectTrue(false, testConfig.name + " - Backward navigation page " + std::to_string(pageIndex),
                          errorMsg);

        if (!testConfig.disableRendering) {
          savePage(pageIndex, "_1");
        }
      } else {
        runner.expectTrue(true, testConfig.name + " - Backward navigation page " + std::to_string(pageIndex), "",
                          true);  // silent=true
      }
    }

    // Stop if we've reached the end of the provider
    if (provider.getChapterPercentage(endPos) >= 1.0f) {
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
    if (pageIndex + 1 >= testConfig.maxPages) {
      std::cerr << "Reached max page limit (" << testConfig.maxPages << "), stopping.\n";
      ++pageIndex;
      break;
    }

    if (testConfig.incrementalMode) {
      // Move one word forward from the start of the current page
      provider.setPosition(pageStart);
      bool dummy;
      layout.test_getNextLineDefault(
          provider, renderer, layoutConfig.pageWidth - layoutConfig.marginLeft - layoutConfig.marginRight, dummy);
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
}

int main(int argc, char* argv[]) {
  TestUtils::TestRunner runner("TextLayout Page Render Test");

  // Initialize global provider and layout from test_globals.h configuration
  if (!TestGlobals::init()) {
    std::cerr << "Failed to initialize test globals\n";
    return 2;
  }

  // Create display with dummy pins
  EInkDisplay display(::TestConfig::DUMMY_PIN, ::TestConfig::DUMMY_PIN, ::TestConfig::DUMMY_PIN,
                      ::TestConfig::DUMMY_PIN, ::TestConfig::DUMMY_PIN, ::TestConfig::DUMMY_PIN);

  // Initialize (no-op for many functions in desktop stubs)
  display.begin();

  // Clear to white (0xFF is white in driver)
  display.clearScreen(0xFF);

  // Initialize font glyph maps for fast lookup
  initFontGlyphMap(&NotoSans26);

  // Render some text onto the frame buffer using the TextRenderer
  TextRenderer renderer(display);
  renderer.setFont(&NotoSans26);
  renderer.setTextColor(TextRenderer::COLOR_BLACK);
  renderer.setFrameBuffer(display.getFrameBuffer());
  renderer.setBitmapType(TextRenderer::BITMAP_BW);

  // Ensure output directory exists
  std::filesystem::create_directories(::TestConfig::TEST_OUTPUT_DIR);

  // Define test configurations
  std::vector<PageTestConfig> testConfigs = {
      {"Incremental All Pages - No Render", true,  true,  0, 5000},
      {"Normal - No Render",                false, true,  0, 5000},
      {"Normal - With Render",              false, false, 0, 5000},
  };

  // Run all test configurations
  for (const auto& config : testConfigs) {
    runTestConfiguration(config, runner, display, renderer);
  }

  // Cleanup
  TestGlobals::cleanup();

  return runner.allPassed() ? 0 : 1;
}
