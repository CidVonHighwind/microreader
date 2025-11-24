#include <filesystem>
#include <iostream>

#include "../src/EInkDisplay.h"
#include "../src/Fonts/FreeSans12pt7b.h"
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
  // Use our local FreeSans font
  renderer.setFont(&FreeSans12pt7b);
  renderer.setTextColor(TextRenderer::COLOR_BLACK);
  renderer.setCursor(20, 10);
  renderer.print(" ");
  // renderer.setCursor(20, 30);
  // renderer.print("This is a host test output.");
  // Push buffer to display (no-op for host driver, but exercises code paths)
  display.displayBuffer(EInkDisplay::FAST_REFRESH);

  // Save framebuffer as PBM to the project test folder
  // Ensure `test/output` exists so the host test can write the file there
  std::filesystem::create_directories("test/output");
  const char* out = "test/output/test_output.pbm";
  display.saveFrameBufferAsPBM(out);

  return 0;
}
