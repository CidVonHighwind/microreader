#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../src/EInkDisplay.h"
#include "../src/Fonts/NotoSans26.h"
#include "../src/screens/text view/KnuthPlassLayoutStrategy.h"
#include "../src/screens/text view/StringWordProvider.h"
#include "../src/text_renderer/TextRenderer.h"

static std::string joinLine(const std::vector<LayoutStrategy::Word>& line) {
  std::string out;
  for (size_t i = 0; i < line.size(); ++i) {
    out += line[i].text.c_str();
    if (i + 1 < line.size())
      out += ' ';
  }
  return out;
}

static std::string readFile(const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open())
    return {};
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
  std::string path = "data/chapter one.txt";
  std::string content = readFile(path);

  // Convert to Arduino String
  String s(content.c_str());
  StringWordProvider provider(s);

  // Create display + renderer
  EInkDisplay display(-1, -1, -1, -1, -1, -1);
  display.begin();
  TextRenderer renderer(display);
  renderer.setFont(&NotoSans26);

  KnuthPlassLayoutStrategy layout;

  LayoutStrategy::LayoutConfig cfg;
  cfg.marginLeft = 0;
  cfg.marginRight = 0;
  cfg.marginTop = 0;
  cfg.marginBottom = 0;
  cfg.lineHeight = 10;
  cfg.minSpaceWidth = 1;
  cfg.pageWidth = 200;
  cfg.pageHeight = 1000;
  cfg.alignment = LayoutStrategy::ALIGN_LEFT;

  // Run layout to process paragraphs
  layout.layoutText(provider, renderer, cfg);

  // Rewind provider and construct forward string by iterating with default nextline
  provider.setPosition(0);
  const int16_t maxWidth = static_cast<int16_t>(cfg.pageWidth - cfg.marginLeft - cfg.marginRight);

  std::string forward;
  while (provider.hasNextWord()) {
    bool isParagraphEnd = false;
    auto line = layout.test_getNextLineDefault(provider, renderer, maxWidth, isParagraphEnd);
    if (!line.empty()) {
      forward += joinLine(line);
    }
    if (isParagraphEnd)
      forward += '\n';
  }

  std::cerr << "Forward:\n" << forward << "\n";

  // Expect exactly one empty line between the two "Test" paragraphs
  const std::string expected = "Test\n\nTest";
  if (forward != expected) {
    std::cerr << "ERROR: Unexpected forward output for Test\n\nTest.\nExpected: '" << expected << "'\nGot: '" << forward
              << "'\n";
    return 1;
  }

  // Backwards
  provider.setPosition(static_cast<int>(strlen(content)));
  std::string backward;

  while (provider.getCurrentIndex() > 0) {
    bool isParagraphEnd = false;
    auto line = layout.test_getPrevLine(provider, renderer, maxWidth, isParagraphEnd);
    std::string strLine;
    if (!line.empty()) {
      strLine = joinLine(line);
    }
    backward = strLine + backward;
    if (isParagraphEnd)
      backward = '\n' + backward;
  }

  std::cerr << "Backward:\n" << backward << "\n";

  if (forward != backward) {
    std::cerr << "ERROR: Forward and backward outputs differ\n";
    return 1;
  }

  std::cerr << "OK: forward/backward round trip matches\n";
  return 0;
}
