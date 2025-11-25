#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../src/EInkDisplay.h"
#include "../src/Fonts/Font24.h"
#include "../src/screens/text view/GreedyLayoutStrategy.h"
#include "../src/screens/text view/StringWordProvider.h"
#include "../src/text_renderer/TextRenderer.h"

static std::string readFile(const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open())
    return {};
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

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
  std::string path;
  if (argc >= 2)
    path = argv[1];
  else
    path = "data/chapter one.txt";

  std::string content = readFile(path);
  if (content.empty()) {
    std::cerr << "Failed to open '" << path << "'\n";
    return 2;
  }

  // Create two providers for forward and backward scanning
  String s(content.c_str());
  StringWordProvider provider(s);
  StringWordProvider providerB(s);

  // Create display + renderer like other host tests so we can use the real TextRenderer
  EInkDisplay display(-1, -1, -1, -1, -1, -1);
  display.begin();
  TextRenderer renderer(display);
  renderer.setFont(&Font24);

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
  cfg.pageWidth = 480;
  cfg.pageHeight = 480;
  cfg.alignment = LayoutStrategy::ALIGN_LEFT;

  layout.layoutText(provider, renderer, cfg);

  const int16_t maxWidth = static_cast<int16_t>(cfg.pageWidth - cfg.marginLeft - cfg.marginRight);

  // Build a single forward string by iterating from the start using getNextLine
  std::string forward;
  while (provider.hasNextWord()) {
    bool isParagraphEnd = false;
    auto line = layout.test_getNextLine(provider, renderer, maxWidth, isParagraphEnd);

    // add the lines to the forward
    for (size_t i = 0; i < line.size(); ++i) {
      forward += line[i].text.c_str();
    }

    // remove the paragraph end marker from the line
    if (isParagraphEnd)
      forward += '\n';
  }

  std::cerr << "Forward:\n" << forward << "\n";

  std::string backwards;
  while (provider.getCurrentIndex() > 0) {
    bool isParagraphEnd = false;
    auto line = layout.test_getPrevLine(provider, renderer, maxWidth, isParagraphEnd);

    // add the lines to the backwards
    std::string strLine;
    for (size_t i = 0; i < line.size(); ++i) {
      strLine += line[i].text.c_str();
    }
    backwards = strLine + backwards;

    // remove the paragraph end marker from the line
    if (isParagraphEnd)
      backwards = '\n' + backwards;
  }

  std::cerr << "Backward:\n" << backwards << "\n";

  // Compare forward/backward round-trip. Exit non-zero on mismatch.
  if (forward != backwards) {
    std::cerr << "ERROR: Forward and backward outputs differ\n";
    std::cerr << "=== Forward ===\n" << forward << "\n";
    std::cerr << "=== Backward ===\n" << backwards << "\n";
    return 1;
  }

  std::cerr << "OK: Forward and backward outputs are identical\n";
  return 0;
}
