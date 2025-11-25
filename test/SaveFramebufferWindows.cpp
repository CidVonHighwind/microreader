#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "../src/EInkDisplay.h"
#include "../src/Fonts/Font16.h"
#include "../src/Fonts/Font20.h"
#include "../src/Fonts/Font24.h"
#include "../src/Fonts/Font27.h"
#include "../src/screens/text view/GreedyLayoutStrategy.h"
#include "../src/screens/text view/StringWordProvider.h"
#include "../src/text_renderer/TextRenderer.h"
#include "WString.h"

// For test build convenience we include some implementation units so the
// single-file test binary links without changing the global build tasks.
#include "../src/screens/text view/GreedyLayoutStrategy.cpp"
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
  renderer.setFont(&Font24);
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
  bool drewAny = false;

  auto savePage = [&](int pageIndex) {
    // refresh (no-op for host stubs) then save
    display.displayBuffer(EInkDisplay::FAST_REFRESH);
    std::ostringstream ss;
    ss << "test/output/test_output_" << pageIndex << ".pbm";
    std::string out = ss.str();
    display.saveFrameBufferAsPBM(out.c_str());
    std::cout << "Saved page " << pageIndex << " -> " << out << "\n";
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
  GreedyLayoutStrategy layout;
  LayoutStrategy::LayoutConfig config;
  config.marginLeft = 10;
  config.marginRight = 10;
  config.marginTop = 40;
  config.marginBottom = 20;
  config.lineHeight = 30;
  config.minSpaceWidth = 8;
  config.pageWidth = 480;
  config.pageHeight = 800;
  config.alignment = LayoutStrategy::ALIGN_LEFT;

  int pageIndex = 0;
  // Layout pages until provider is exhausted
  while (provider.getPercentage(pageIndex) < 1.0f) {
    display.clearScreen(0xFF);

    provider.setPosition(pageIndex);
    pageIndex = layout.layoutText(provider, renderer, config);

    savePage(page);
    ++page;
    drewAny = true;
  }

  return 0;
}
