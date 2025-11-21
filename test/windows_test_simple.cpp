// Simplified Windows test harness for TextLayout debugging
// This file only contains the test logic - all mocks are in separate files

// Enable debug output for layout algorithm
#define DEBUG_LAYOUT

#include <chrono>
#include <cstdarg>
#include <fstream>
#include <iostream>
#include <vector>

// Include mock implementations (they check for ARDUINO internally)
#include <fstream>

#include "Adafruit_GFX.h"
#include "Arduino.h"
#include "WString.h"

// Define Serial object
MockSerial Serial;

// Mock Arduino functions
unsigned long millis() {
  static auto start = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
  return static_cast<unsigned long>(elapsed.count());
}

// Mock EInkDisplay
class EInkDisplay {
 public:
  static constexpr int16_t DISPLAY_WIDTH = 800;
  static constexpr int16_t DISPLAY_HEIGHT = 480;
  static constexpr int16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;

 private:
  std::vector<uint8_t> frameBuffer;

 public:
  EInkDisplay() : frameBuffer(DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT, 0xFF) {}

  uint8_t* getFrameBuffer() {
    return frameBuffer.data();
  }
  void clearScreen(uint8_t value) {
    std::fill(frameBuffer.begin(), frameBuffer.end(), value);
  }
  void displayBuffer(bool fullRefresh) {
    std::cout << "[Display buffer updated (fullRefresh=" << fullRefresh << ")]" << std::endl;
  }

  // Save frame buffer as PPM image
  void saveAsPPM(const char* filename) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
      std::cerr << "Failed to open " << filename << std::endl;
      return;
    }

    // PPM header (P6 = binary RGB)
    file << "P6\n" << DISPLAY_WIDTH << " " << DISPLAY_HEIGHT << "\n255\n";

    // Write pixels (convert 1-bit to RGB)
    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
      for (int x = 0; x < DISPLAY_WIDTH; x++) {
        int byteIndex = y * DISPLAY_WIDTH_BYTES + (x / 8);
        int bitPosition = 7 - (x % 8);
        bool isWhite = (frameBuffer[byteIndex] >> bitPosition) & 1;

        // Write RGB values (white=255,255,255 black=0,0,0)
        unsigned char color = isWhite ? 255 : 0;
        file.write((char*)&color, 1);
        file.write((char*)&color, 1);
        file.write((char*)&color, 1);
      }
    }

    file.close();
    std::cout << "Saved frame buffer to " << filename << std::endl;
  }
};

// Mock TextRenderer
class TextRenderer : public Adafruit_GFX {
 public:
  static const uint16_t COLOR_BLACK = 0;
  static const uint16_t COLOR_WHITE = 1;

  TextRenderer(EInkDisplay& display)
      : Adafruit_GFX(EInkDisplay::DISPLAY_HEIGHT, EInkDisplay::DISPLAY_WIDTH), display(display) {
    Serial.printf("[%lu] TextRenderer: Constructor called (portrait 480x800)\n", millis());
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || x >= EInkDisplay::DISPLAY_HEIGHT || y < 0 || y >= EInkDisplay::DISPLAY_WIDTH) {
      return;
    }

    // Rotate -180 degrees: portrait (480x800) -> landscape (800x480)
    int16_t rotatedX = y;
    int16_t rotatedY = EInkDisplay::DISPLAY_HEIGHT - 1 - x;

    uint8_t* frameBuffer = display.getFrameBuffer();
    uint16_t byteIndex = rotatedY * EInkDisplay::DISPLAY_WIDTH_BYTES + (rotatedX / 8);
    uint8_t bitPosition = 7 - (rotatedX % 8);

    if (color == COLOR_BLACK) {
      frameBuffer[byteIndex] &= ~(1 << bitPosition);
    } else {
      frameBuffer[byteIndex] |= (1 << bitPosition);
    }
  }

  void clearText() {
    display.clearScreen(0xFF);
  }
  void refresh(bool fullRefresh = false) {
    display.displayBuffer(fullRefresh);
  }

 private:
  EInkDisplay& display;
};

// Include the actual implementation headers
#include "../src/GreedyLayoutStrategy.h"
#include "../src/LayoutStrategy.h"
#include "../src/TextLayout.h"

// Include the font
#include "FreeSans12pt7b.h"

// Test sample text - matches the actual sample_text.h from the device
const char* TEST_TEXT = R"(Chapter 1: The Beginning

In the realm of electronic paper displays, there existed a device known as the MicroReader. This remarkable gadget combined the comfort of reading traditional books with the convenience of modern technology. The e-ink screen displayed text with remarkable clarity, mimicking the appearance of ink on paper. No harsh backlight strained the eyes during long reading sessions. Readers could enjoy their favorite books for hours without fatigue.

The technology behind e-ink displays was fascinating. Tiny microcapsules containing black and white particles responded to electrical charges, creating images that remained visible without power. This bistable nature meant the display only consumed energy when changing pages, allowing for weeks of battery life. The contrast ratio rivaled printed paper, making text crisp and legible even in direct sunlight.

---PAGE---

Chapter 2: Discovery

Engineers had spent years perfecting the technology. The custom LUT configurations allowed for rapid page refreshes, making the reading experience smooth and natural. Partial refresh modes eliminated the distracting full-screen flashes that plagued earlier generations of e-readers.

Chapter 3: Innovation

The device featured custom fonts rendered through Adafruit-GFX, providing crisp, professional typography. FreeSans fonts in multiple sizes offered excellent readability. Button navigation made page turning effortless.)";

void saveFrameBufferAsPBM(const EInkDisplay& display, const char* filename) {
  const uint8_t* buffer = const_cast<EInkDisplay&>(display).getFrameBuffer();

  std::ofstream file(filename, std::ios::binary);
  if (!file) {
    std::cerr << "Failed to open " << filename << " for writing" << std::endl;
    return;
  }

  // Rotate the image 90 degrees counterclockwise when saving
  // Original buffer: 800x480 (landscape)
  // Output image: 480x800 (portrait)
  file << "P4\n";  // Binary PBM
  file << EInkDisplay::DISPLAY_HEIGHT << " " << EInkDisplay::DISPLAY_WIDTH << "\n";

  // Create rotated buffer
  std::vector<uint8_t> rotatedBuffer((EInkDisplay::DISPLAY_HEIGHT / 8) * EInkDisplay::DISPLAY_WIDTH, 0);

  // Rotate: for each pixel in output, find corresponding pixel in input
  for (int outY = 0; outY < EInkDisplay::DISPLAY_WIDTH; outY++) {
    for (int outX = 0; outX < EInkDisplay::DISPLAY_HEIGHT; outX++) {
      // Map output coordinates to input coordinates (-90 degrees = 270 degrees = 90 degrees clockwise)
      int inX = outY;
      int inY = EInkDisplay::DISPLAY_HEIGHT - 1 - outX;

      // Read pixel from input buffer
      int inByteIndex = inY * EInkDisplay::DISPLAY_WIDTH_BYTES + (inX / 8);
      int inBitPosition = 7 - (inX % 8);
      bool isWhite = (buffer[inByteIndex] >> inBitPosition) & 1;

      // Write pixel to output buffer (inverted because PBM uses opposite convention)
      int outByteIndex = outY * (EInkDisplay::DISPLAY_HEIGHT / 8) + (outX / 8);
      int outBitPosition = 7 - (outX % 8);
      if (!isWhite) {  // Invert: e-ink white=1 -> PBM black=1
        rotatedBuffer[outByteIndex] |= (1 << outBitPosition);
      }
    }
  }

  // Write the rotated and inverted buffer
  file.write(reinterpret_cast<const char*>(rotatedBuffer.data()), rotatedBuffer.size());

  file.close();
  std::cout << "Saved framebuffer to " << filename << std::endl;
  std::cout << "You can view it with an image viewer that supports PBM format," << std::endl;
  std::cout << "or convert it with: magick " << filename << " output.png" << std::endl;
}

void printLayoutInfo(const TextLayout& layout, const TextRenderer& renderer) {
  (void)layout;  // Unused for now
  (void)renderer;
  std::cout << "\n=== Layout Information ===" << std::endl;
  std::cout << "Text is being rendered using the FreeSans12pt7b font." << std::endl;
  std::cout << "The Knuth-Plass algorithm calculates optimal line breaks and word spacing," << std::endl;
  std::cout << "and the actual font glyphs are rendered to the frame buffer." << std::endl;
  std::cout << "The output should match what you see on the real device." << std::endl;
}

int main() {
  std::cout << "TextLayout Windows Test Harness" << std::endl;
  std::cout << "===============================" << std::endl;

  // Create mock display and renderer
  EInkDisplay display;
  TextRenderer renderer(display);

  // Set up font to match the real device (FreeSans12pt7b)
  renderer.setFont(&FreeSans12pt7b);
  renderer.setTextColor(TextRenderer::COLOR_BLACK);

  // Create layout engine
  TextLayout layout;

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

  std::cout << "\nLayout Configuration:" << std::endl;
  std::cout << "  Page size: " << config.pageWidth << "x" << config.pageHeight << std::endl;
  std::cout << "  Margins: L=" << config.marginLeft << " R=" << config.marginRight << " T=" << config.marginTop
            << " B=" << config.marginBottom << std::endl;
  std::cout << "  Line height: " << config.lineHeight << std::endl;
  std::cout << "  Min space width: " << config.minSpaceWidth << std::endl;

  // Test the layout
  std::cout << "\nPerforming text layout..." << std::endl;

  // Split by ---PAGE--- delimiter like the real device does
  std::string fullTextStr(TEST_TEXT);
  size_t pageDelimiter = fullTextStr.find("---PAGE---");
  std::string textStr = (pageDelimiter != std::string::npos) ? fullTextStr.substr(0, pageDelimiter) : fullTextStr;

  // Trim whitespace
  size_t trimStart = textStr.find_first_not_of(" \t\n\r");
  size_t trimEnd = textStr.find_last_not_of(" \t\n\r");
  if (trimStart != std::string::npos && trimEnd != std::string::npos) {
    textStr = textStr.substr(trimStart, trimEnd - trimStart + 1);
  }

  String text(textStr.c_str());
  std::cout << "Rendering first page only (" << textStr.length() << " characters)\n" << std::endl;

  auto start = std::chrono::high_resolution_clock::now();
  layout.layoutText(text, renderer, config);
  auto end = std::chrono::high_resolution_clock::now();

  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  std::cout << "\nLayout completed in " << duration.count() << " microseconds" << std::endl;

  printLayoutInfo(layout, renderer);

  // Save to file
  saveFrameBufferAsPBM(display, "output.pbm");

  // You can add interactive debugging here
  std::cout << "\n=== Test Complete ===" << std::endl;

  return 0;
}
