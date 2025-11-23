#include "DisplayController.h"

#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>

#include "KnuthPlassLayoutStrategy.h"
#include "TextLayout.h"
#include "bebop_2.h"
#include "bebop_image.h"
#include "sample_text.h"

DisplayController::DisplayController(EInkDisplay& display)
    : display(display),
      textRenderer(display),
      textLayout(new KnuthPlassLayoutStrategy()),  // Use Knuth-Plass optimal line breaking
      currentMode(READER),
      currentScreen(IMAGE),
      currentPage(0),
      totalPages(0) {
  Serial.printf("[%lu] DisplayController: Constructor called\n", millis());
}

void DisplayController::begin() {
  Serial.printf("[%lu] DisplayController: begin() called\n", millis());

  // Load text from PROGMEM
  loadTextFile();

  // Display first page in reader mode on startup
  if (currentMode == READER) {
    showReaderPage(currentPage);
  } else {
    showScreen(currentScreen);
  }
  display.displayBuffer(EInkDisplay::HALF_REFRESH);

  Serial.printf("[%lu] DisplayController initialized\n", millis());
}

void DisplayController::showScreen(Screen screen) {
  currentScreen = screen;

  switch (screen) {
    case WHITE:
      Serial.printf("[%lu] Showing WHITE screen\n", millis());
      display.clearScreen(0xFF);
      break;

    case BLACK:
      Serial.printf("[%lu] Showing BLACK screen\n", millis());
      display.clearScreen(0x00);
      break;

    case IMAGE:
      Serial.printf("[%lu] Showing IMAGE screen\n", millis());
      display.drawImage(bebop_image, 0, 0, BEBOP_IMAGE_WIDTH, BEBOP_IMAGE_HEIGHT, true);
      break;

    case IMAGE_2:
      Serial.printf("[%lu] Showing IMAGE screen\n", millis());
      display.drawImage(bebop_2, 0, 0, BEBOP_2_WIDTH, BEBOP_2_HEIGHT, true);
      break;
  }

  // Debug: Print complete framebuffer
  // display.debugPrintFramebuffer();
}

void DisplayController::showReaderPage(int page) {
  currentPage = page;
  Serial.printf("[%lu] Showing READER page %d\n", millis(), page);

  textRenderer.clearText();
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFont(&FreeSans12pt7b);

  // Display page content with optimal line breaking
  if (page >= 0 && page < totalPages) {
    String pageText = pages[page];

    // Configure layout
    TextLayout::LayoutConfig config;
    config.marginLeft = 10;
    config.marginRight = 10;
    config.marginTop = 40;
    config.marginBottom = 40;
    config.lineHeight = 30;
    config.minSpaceWidth = 10;
    config.pageWidth = 480;
    config.pageHeight = 800;
    config.alignment = LayoutStrategy::ALIGN_LEFT;

    // Layout and render text
    textLayout.layoutText(pageText, textRenderer, config);
  }

  // Add page indicator at bottom center
  textRenderer.setFont();

  // Create page indicator string
  String pageIndicator = String(page + 1) + "/" + String(totalPages);

  // Measure text to center it
  int16_t x1, y1;
  uint16_t w, h;
  textRenderer.getTextBounds(pageIndicator.c_str(), 0, 0, &x1, &y1, &w, &h);
  int16_t centerX = (480 - w) / 2;

  textRenderer.setCursor(centerX, 780);
  textRenderer.print(pageIndicator);
}

void DisplayController::loadTextFile() {
  Serial.printf("[%lu] Loading text from PROGMEM...\n", millis());

  // Read text from PROGMEM
  String fullText = "";
  char c;
  int i = 0;
  while ((c = pgm_read_byte(&SAMPLE_TEXT[i])) != '\0') {
    fullText += c;
    i++;
  }

  // Parse text into pages
  String currentPageText = "";
  int startIndex = 0;

  while (startIndex < fullText.length()) {
    int pageDelimiter = fullText.indexOf("---PAGE---", startIndex);

    if (pageDelimiter == -1) {
      // Last page
      currentPageText = fullText.substring(startIndex);
      currentPageText.trim();
      if (currentPageText.length() > 0) {
        pages.push_back(currentPageText);
      }
      break;
    } else {
      // Found delimiter
      currentPageText = fullText.substring(startIndex, pageDelimiter);
      currentPageText.trim();
      if (currentPageText.length() > 0) {
        pages.push_back(currentPageText);
      }
      startIndex = pageDelimiter + 10;  // Skip "---PAGE---"
    }
  }

  totalPages = pages.size();
  Serial.printf("[%lu] Loaded %d pages from PROGMEM\n", millis(), totalPages);
}

void DisplayController::switchMode() {
  if (currentMode == DEMO) {
    currentMode = READER;
    currentPage = 0;
    Serial.printf("[%lu] Switched to READER mode\n", millis());
    showReaderPage(currentPage);
  } else {
    currentMode = DEMO;
    currentScreen = IMAGE;
    Serial.printf("[%lu] Switched to DEMO mode\n", millis());
    showScreen(currentScreen);
  }
}

void DisplayController::showSleepScreen() {
  Serial.printf("[%lu] Showing SLEEP screen\n", millis());
  display.clearScreen(0xFF);

  // Draw bebop image centered
  display.drawImage(bebop_image, 0, 0, BEBOP_IMAGE_WIDTH, BEBOP_IMAGE_HEIGHT, true);

  // Add "Sleeping..." text at the bottom
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFont(&FreeSans12pt7b);

  const char* sleepText = "Sleeping...";
  int16_t x1, y1;
  uint16_t w, h;
  textRenderer.getTextBounds(sleepText, 0, 0, &x1, &y1, &w, &h);
  int16_t centerX = (480 - w) / 2;

  textRenderer.setCursor(centerX, 780);
  textRenderer.print(sleepText);
}

void DisplayController::handleButtons(Buttons& buttons) {
  if (buttons.wasPressed(Buttons::VOLUME_UP)) {
    display.setCustomLUT(true);
  } else if (buttons.wasPressed(Buttons::VOLUME_DOWN)) {
    display.setCustomLUT(false);
  } else if (buttons.wasPressed(Buttons::CONFIRM)) {
    // Switch between modes
    switchMode();
    display.displayBuffer(EInkDisplay::FAST_REFRESH);
  } else if (buttons.wasPressed(Buttons::BACK)) {
    display.displayBuffer(EInkDisplay::FULL_REFRESH);
  } else if (buttons.wasPressed(Buttons::LEFT)) {
    // Navigate within current mode
    if (currentMode == DEMO) {
      // Navigate to next demo screen (WHITE/BLACK/IMAGE)
      int next = (currentScreen + 1) % 4;
      showScreen(static_cast<Screen>(next));
    } else {
      // Navigate to next page in reader mode (with looping)
      int next = (currentPage + 1) % totalPages;
      showReaderPage(next);
    }
    display.displayBuffer(EInkDisplay::FAST_REFRESH);
  } else if (buttons.wasPressed(Buttons::RIGHT)) {
    // Navigate within current mode
    if (currentMode == DEMO) {
      // Navigate to previous demo screen (WHITE/BLACK/IMAGE)
      int prev = (currentScreen - 1 + 4) % 4;
      showScreen(static_cast<Screen>(prev));
    } else {
      // Navigate to previous page in reader mode (with looping)
      int prev = (currentPage - 1 + totalPages) % totalPages;
      showReaderPage(prev);
    }
    display.displayBuffer(EInkDisplay::FAST_REFRESH);
  }
}
