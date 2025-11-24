#include <filesystem>
#include <iostream>

#include "../src/EInkDisplay.h"
#include "../src/Fonts/Font16.h"
#include "../src/Fonts/Font27.h"
#include "../src/text_renderer/TextRenderer.h"
#include "WString.h"

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
  renderer.setFont(&Font16);
  renderer.setTextColor(TextRenderer::COLOR_BLACK);
  // Print a full printable ASCII range for font testing character-by-character
  // and wrap to the next line when characters do not fit.
  const int margin = 4;
  int x = margin;
  int y = 32;
  const char* ascii =
      " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~äüöÄÜÖß";
  for (const char* p = ascii; *p; ++p) {
    char s[2] = {*p, 0};
    int16_t bx, by;
    uint16_t bw, bh;
    renderer.getTextBounds(s, x, y, &bx, &by, &bw, &bh);
    if (x + (int)bw > (int)EInkDisplay::DISPLAY_HEIGHT - margin) {
      x = margin;
      y += bh + 4;
    }
    renderer.setCursor(x, y);
    renderer.print(s);
    x += bw;
  }

  display.displayBuffer(EInkDisplay::FAST_REFRESH);

  // Save framebuffer as PBM to the project test folder
  // Ensure `test/output` exists so the host test can write the file there
  std::filesystem::create_directories("test/output");
  const char* out = "test/output/test_output.pbm";
  display.saveFrameBufferAsPBM(out);

  return 0;
}
