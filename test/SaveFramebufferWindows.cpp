#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/EInkDisplay.h"
#include "../src/Fonts/NotoSans26.h"
#include "../src/screens/text view/GreedyLayoutStrategy.h"
#include "../src/screens/text view/KnuthPlassLayoutStrategy.h"
#include "../src/screens/text view/StringWordProvider.h"
#include "../src/text_renderer/TextRenderer.h"
#include "WString.h"

// For test build convenience we include some implementation units so the
// single-file test binary links without changing the global build tasks.
#include "../src/screens/text view/GreedyLayoutStrategy.cpp"
#include "../src/screens/text view/KnuthPlassLayoutStrategy.cpp"
#include "../src/screens/text view/StringWordProvider.cpp"

int main() {
  std::cout << "Starting SaveFramebufferWindows test...\n";

  // Create display with dummy pins (-1 indicates unused in host stubs)
  EInkDisplay display(-1, -1, -1, -1, -1, -1);

  // Initialize (no-op for many functions in desktop stubs)
  display.begin();

  // Clear to white (0xFF is white in driver)
  display.clearScreen(0xFF);

  // Render some text onto the frame buffer using the TextRenderer
  TextRenderer renderer(display);
  // Use our local font
  renderer.setFont(&NotoSans26);
  renderer.setTextColor(TextRenderer::COLOR_BLACK);
  // Print the contents of `data/font test.txt` character-by-character
  // and wrap to the next line when characters do not fit. When the page
  // (vertical space) fills, save the current framebuffer and start a new
  // page. Always use the .txt file; error out if it cannot be opened.
  const int margin = 4;
  int x = margin;
  int y = 32;
  const std::string filepath = "data/chapter one.txt";
  std::ifstream infile(filepath);
  if (!infile) {
    std::cerr << "Failed to open '" << filepath << "'\n";
    return 1;
  }

  // Ensure output directory exists
  std::filesystem::create_directories("test/output");

  int page = 0;

  auto savePage = [&](int pageIndex, String postfix) {
    // Save the currently rendered framebuffer first. `displayBuffer`
    // swaps the internal buffers, so calling it before saving would
    // write the (now swapped) inactive/empty buffer to disk.
    std::ostringstream ss;
    ss << "test/output/output_" << std::setw(3) << std::setfill('0') << pageIndex << std::setfill(' ')
       << postfix.c_str() << ".pbm";
    std::string out = ss.str();
    display.saveFrameBufferAsPBM(out.c_str());
  };

  // Read entire file into memory and use StringWordProvider + TextLayout
  std::string content((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
  String fullText(content.c_str());

  // Precompute a typical line height for spacing and populate layout config
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  renderer.getTextBounds("Ag", x, y, &tbx, &tby, &tbw, &tbh);
  const int lineSpacing = (int)tbh + 4;

  StringWordProvider provider(fullText);
  // GreedyLayoutStrategy layout;
  KnuthPlassLayoutStrategy layout;
  LayoutStrategy::LayoutConfig layoutConfig;
  layoutConfig.marginLeft = 10;
  layoutConfig.marginRight = 10;
  layoutConfig.marginTop = 40;
  layoutConfig.marginBottom = 20;
  layoutConfig.lineHeight = 30;
  layoutConfig.minSpaceWidth = 8;
  layoutConfig.pageWidth = 480;
  layoutConfig.pageHeight = 800;
  layoutConfig.alignment = LayoutStrategy::ALIGN_LEFT;

  // Traverse the entire document forward, saving each page's start and end indices
  std::vector<std::pair<int, int>> pageRanges;  // pair<start, end>
  int pageStart = 0;
  int pageIndex = 0;

  while (true) {
    display.clearScreen(0xFF);

    provider.setPosition(pageStart);

    int endPos = layout.layoutText(provider, renderer, layoutConfig);
    // record the start and end positions for this page
    pageRanges.push_back(std::make_pair(pageStart, endPos));

    savePage(pageIndex, "_0");

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

    pageStart = endPos;
    ++pageIndex;
  }

  std::cout << "Forward traversal produced " << pageRanges.size() << " pages.\n";

  // Write out all page start/end positions to console and a file for inspection
  std::ofstream rangesOut("test/output/page_ranges.txt");
  if (!rangesOut) {
    std::cerr << "Warning: unable to open test/output/page_ranges.txt for writing\n";
  }
  for (size_t i = 0; i < pageRanges.size(); ++i) {
    int s = pageRanges[i].first;
    int e = pageRanges[i].second;
    if (rangesOut)
      rangesOut << i << " " << s << " " << e << "\n";
  }
  if (rangesOut)
    rangesOut.close();

  // Move backward from the last page and verify previous page starts and ends
  bool mismatch = false;
  pageIndex--;

  for (int i = (int)pageRanges.size() - 1; i > 0; --i) {
    int currentStart = pageRanges[i].first;
    int expectedPrevStart = pageRanges[i - 1].first;
    int expectedPrevEnd = pageRanges[i - 1].second;

    int computedPrevStart = layout.getPreviousPageStart(provider, renderer, layoutConfig, currentStart);

    // Render the computed previous page to determine its end position and save it
    display.clearScreen(0xFF);
    provider.setPosition(computedPrevStart);
    int computedPrevEnd = layout.layoutText(provider, renderer, layoutConfig);
    pageIndex--;
    savePage(pageIndex, "_1");

    bool startMatch = (computedPrevStart == expectedPrevStart);
    bool endMatch = (computedPrevEnd == expectedPrevEnd);

    if (!startMatch || !endMatch) {
      std::cerr << "Mismatch at page " << i << ":\n"
                << "  computedPrevStart=" << computedPrevStart << " expectedPrevStart=" << expectedPrevStart << "\n"
                << "  computedPrevEnd=" << computedPrevEnd << " expectedPrevEnd=" << expectedPrevEnd << "\n";
      mismatch = true;
    }
  }

  if (mismatch) {
    std::cerr << "One or more previous-page range checks failed.\n";
    return 2;
  }

  return 0;
}
