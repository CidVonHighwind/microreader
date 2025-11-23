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
  // Refresh modes
  enum RefreshMode {
    FULL_REFRESH,  // Full refresh with complete waveform
    HALF_REFRESH,  // Half refresh (1720ms) - balanced quality and speed
    FAST_REFRESH   // Fast refresh using custom LUT
  };

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
  void displayBuffer(RefreshMode mode = FAST_REFRESH) {
    const char* modeStr = (mode == FULL_REFRESH) ? "FULL" : (mode == HALF_REFRESH) ? "HALF" : "FAST";
    std::cout << "[Display buffer updated (mode=" << modeStr << ")]" << std::endl;
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
  void refresh(EInkDisplay::RefreshMode mode = EInkDisplay::FAST_REFRESH) {
    display.displayBuffer(mode);
  }

 private:
  EInkDisplay& display;
};

// Include the actual implementation headers
#include "../src/KnuthPlassLayoutStrategy.h"
#include "../src/LayoutStrategy.h"
#include "../src/TextLayout.h"
#include "../src/sample_text.h"

// Include the font
#include "FreeSans12pt7b.h"

// Use the same text from the device
const char* TEST_TEXT = SAMPLE_TEXT;

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

  // Split text into pages by ---PAGE--- delimiter
  std::string fullTextStr(TEST_TEXT);
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
    EInkDisplay display;
    TextRenderer renderer(display);
    renderer.setFont(&FreeSans12pt7b);
    renderer.setTextColor(TextRenderer::COLOR_BLACK);

    // Create layout engine
    TextLayout layout;

    String text(pages[pageNum].c_str());

    auto startTime = std::chrono::high_resolution_clock::now();
    layout.layoutText(text, renderer, config);
    auto endTime = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    std::cout << "Layout completed in " << duration.count() << " microseconds" << std::endl;

    // Save to file with page number
    std::string filename = "output_page_" + std::to_string(pageNum + 1) + ".pbm";
    saveFrameBufferAsPBM(display, filename.c_str());
    std::cout << std::endl;
  }

  printLayoutInfo(TextLayout(), TextRenderer(*new EInkDisplay()));

  std::cout << "\n=== Test Complete ===" << std::endl;
  std::cout << "Generated " << pages.size() << " output files." << std::endl;

  return 0;
}
