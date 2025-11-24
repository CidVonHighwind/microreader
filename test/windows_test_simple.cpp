// Simplified Windows test harness for TextLayout debugging
// This file only contains the test logic - all mocks are in separate files

// Enable debug output for layout algorithm
#define DEBUG_LAYOUT

#define TEST_BUILD

#include <chrono>
#include <cstdarg>
#include <fstream>
#include <iostream>
#include <vector>

// Include mock implementations (they check for ARDUINO internally)
#include <fstream>

#include "../src/EInkDisplay.h"
#include "Adafruit_GFX.h"
#include "Arduino.h"
#include "WString.h"
#include "gfxfont.h"

// Define Serial object
MockSerial Serial;

// Mock Arduino functions
unsigned long millis() {
  static auto start = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
  return static_cast<unsigned long>(elapsed.count());
}

// RefreshMode is provided by `EInkDisplay.h`

// Use the real `EInkDisplay` implementation from `src/` for the test build.

// Include the actual implementation headers
#include "../src/TextRenderer.h"
#include "../src/screens/text view/GreedyLayoutStrategy.h"
#include "../src/screens/text view/KnuthPlassLayoutStrategy.h"
#include "../src/screens/text view/LayoutStrategy.h"
#include "../src/screens/text view/StringWordProvider.h"
#include "../src/screens/text view/TextLayout.h"

// Include the font
#include "FreeSans12pt7b.h"

// Load test text from the data folder. If the file cannot be opened,
// fall back to a short inline sample text.
std::string loadTestText() {
  // Try a few likely relative paths so the test exe finds the data when
  // executed from the build output directory (e.g. test/build_msvc/Release).
  const std::vector<std::string> candidates = {"../../data/chapter one.txt",  // when running from test/build_msvc
                                               "../data/chapter one.txt",     // when running from test/
                                               "data/chapter one.txt",        // when running from repo root
                                               "../data/chapter one.txt"};    // fallback

  for (const auto& path : candidates) {
    std::ifstream infile(path, std::ios::in | std::ios::binary);
    if (infile) {
      std::string contents;
      infile.seekg(0, std::ios::end);
      std::streampos size = infile.tellg();
      if (size > 0)
        contents.reserve((size_t)size);
      infile.seekg(0, std::ios::beg);
      contents.assign((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
      std::cout << "Loaded test text from '" << path << "' (" << contents.size() << " bytes)" << std::endl;
      return contents;
    }
  }

  std::cerr << "Warning: failed to open chapter text in data folder. Using fallback text." << std::endl;
  return std::string(
      "This is a test text for layout. It has multiple sentences. This should work for testing the layout algorithm on "
      "Windows.");
}

// Use the EInkDisplay helper to save to PBM instead of duplicating logic here.

void printLayoutInfo(const TextLayout& layout, const TextRenderer& renderer) {
  (void)layout;  // Unused for now
  (void)renderer;
  std::cout << "\n=== Layout Information ===" << std::endl;
  std::cout << "Text is being rendered using the FreeSans12pt7b font." << std::endl;
  std::cout << "The Knuth-Plass algorithm calculates optimal line breaks and word spacing," << std::endl;
  std::cout << "and the actual font glyphs are rendered to the frame buffer." << std::endl;
  std::cout << "The output should match what you see on the real device." << std::endl;
}

int main(int argc, char** argv) {
  std::cout << "TextLayout Windows Test Harness" << std::endl;
  std::cout << "===============================" << std::endl;

  // Configure layout
  TextLayout::LayoutConfig config;
  config.pageWidth = 480;
  config.pageHeight = 800;
  config.marginLeft = 10;
  config.marginRight = 10;
  config.marginTop = 40;
  config.marginBottom = 40;
  config.lineHeight = 30;
  config.minSpaceWidth = 10;
  config.alignment = LayoutStrategy::ALIGN_LEFT;

  std::cout << "\nLayout Configuration:" << std::endl;
  std::cout << "  Page size: " << config.pageWidth << "x" << config.pageHeight << std::endl;
  std::cout << "  Margins: L=" << config.marginLeft << " R=" << config.marginRight << " T=" << config.marginTop
            << " B=" << config.marginBottom << std::endl;
  std::cout << "  Line height: " << config.lineHeight << std::endl;
  std::cout << "  Min space width: " << config.minSpaceWidth << std::endl;

  // Load the text to render from the data file (or fallback)
  std::string fullTextStr = loadTestText();
  std::vector<std::string> pages;
  size_t start = 0;

  while (start < fullTextStr.length()) {
    size_t pageDelimiter = fullTextStr.find("---PAGE---", start);
    std::string pageText;

    if (pageDelimiter != std::string::npos) {
      pageText = fullTextStr.substr(start, pageDelimiter - start);
      start = pageDelimiter + 10;  // Skip past "---PAGE---"
    } else {
      pageText = fullTextStr.substr(start);
      start = fullTextStr.length();
    }

    // Trim whitespace
    size_t trimStart = pageText.find_first_not_of(" \t\n\r");
    size_t trimEnd = pageText.find_last_not_of(" \t\n\r");
    if (trimStart != std::string::npos && trimEnd != std::string::npos) {
      pageText = pageText.substr(trimStart, trimEnd - trimStart + 1);
      pages.push_back(pageText);
    }
  }

  std::cout << "\nFound " << pages.size() << " pages in test text\n" << std::endl;

  // Render each page
  for (size_t pageNum = 0; pageNum < pages.size(); pageNum++) {
    std::cout << "=== Rendering Page " << (pageNum + 1) << " ===" << std::endl;
    std::cout << "Characters: " << pages[pageNum].length() << std::endl;

    // Create fresh display and renderer for each page
    // Provide dummy pin numbers for the hardware constructor when testing on PC.
    EInkDisplay display(0, 1, 2, 3, 4, 5);
    TextRenderer renderer(display);
    renderer.setFont(&FreeSans12pt7b);
    renderer.setTextColor(TextRenderer::COLOR_BLACK);

    // Create layout engine. Default to GreedyLayoutStrategy; allow override
    // via command-line: pass "--knuth" to use Knuth-Plass instead.
    TextLayout layout;
    bool useKnuth = false;
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "--knuth") {
        useKnuth = true;
        break;
      }
    }

    if (useKnuth) {
      layout.setStrategy(new KnuthPlassLayoutStrategy());
      std::cout << "Using Knuth-Plass layout strategy" << std::endl;
    } else {
      // ensure Greedy strategy is used explicitly
      layout.setStrategy(new GreedyLayoutStrategy());
      std::cout << "Using Greedy layout strategy" << std::endl;
    }

    StringWordProvider provider(pages[pageNum].c_str());

    auto startTime = std::chrono::high_resolution_clock::now();
    layout.layoutText(provider, renderer, config);
    auto endTime = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    std::cout << "Layout completed in " << duration.count() << " microseconds" << std::endl;

    // Save to file with page number
    std::string filename = "output_page_" + std::to_string(pageNum + 1) + ".pbm";
    display.saveFrameBufferAsPBM(filename.c_str());
    std::cout << std::endl;
  }

  EInkDisplay tempDisplay(0, 1, 2, 3, 4, 5);
  printLayoutInfo(TextLayout(), TextRenderer(tempDisplay));

  std::cout << "\n=== Test Complete ===" << std::endl;
  std::cout << "Generated " << pages.size() << " output files." << std::endl;

  return 0;
}
