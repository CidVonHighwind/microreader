#include "MenuDisplay.h"

#include "bebop_image.h"

MenuDisplay::MenuDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy)
    : _sclk(sclk),
      _mosi(mosi),
      _cs(cs),
      _dc(dc),
      _rst(rst),
      _busy(busy),
      display(nullptr),
      epd(nullptr),
      bebopImageVisible(false) {}

MenuDisplay::~MenuDisplay() {
  if (display) {
    delete display;
  }
  if (epd) {
    delete epd;
  }
}

void MenuDisplay::begin() {
  Serial.println("Initializing display driver...");

  // Create e-ink driver instance
  epd = new EInk426_BW(_cs, _dc, _rst, _busy);

  // Create display wrapper with GFX support
  display = new EInk_BW_Display<EInk426_BW, EInk426_BW::HEIGHT>(*epd);

  // Initialize SPI with custom pins
  SPI.begin(_sclk, -1, _mosi, _cs);

  // Initialize display (reset duration reduced to 1 for faster init)
  display->init(115200, true, 1, false);

  // Setup display text properties
  display->setRotation(3);
  display->setTextColor(GxEPD_BLACK);
  display->setTextSize(2);

  Serial.println("Display driver initialized");
}

void MenuDisplay::drawMenuItem(const char* itemText, int y, bool isSelected) {
  display->setCursor(15, y);
  if (isSelected) {
    display->print(">");
    display->print(itemText);
    display->print("<");
  } else {
    display->print(itemText);
  }
}

void MenuDisplay::drawFullMenu(const char** menuItems, int menuCount, int selectedIndex) {
  display->setFullWindow();
  display->firstPage();
  do {
    display->fillScreen(GxEPD_WHITE);
    display->setTextSize(3);
    display->setCursor(30, 30);
    display->println("Library");

    // Draw menu items
    display->setTextSize(2);
    for (int i = 0; i < menuCount; i++) {
      int y = menuStartY + (i * lineHeight);
      drawMenuItem(menuItems[i], y, i == selectedIndex);
    }
  } while (display->nextPage());
}

void MenuDisplay::updateCursor(const char** menuItems, int menuCount, int oldIndex, int newIndex) {
  // Calculate the region that covers both old and new cursor positions
  int minIndex = min(oldIndex, newIndex);
  int maxIndex = max(oldIndex, newIndex);

  int y = menuStartY + (minIndex * lineHeight);
  int h = ((maxIndex - minIndex) + 1) * lineHeight - 5;
  int16_t x = 15;
  int16_t w = 300;  // Width to cover full text lines

  // Single partial update for the affected text lines
  display->setPartialWindow(x, y - 5, w, h);
  display->firstPage();
  do {
    display->fillScreen(GxEPD_WHITE);
    display->setTextSize(2);

    // Redraw text lines with cursor prefix
    for (int i = minIndex; i <= maxIndex; i++) {
      int itemY = menuStartY + (i * lineHeight);
      drawMenuItem(menuItems[i], itemY, i == newIndex);
    }
  } while (display->nextPage());
}

void MenuDisplay::handleVolumeUp() {
  Serial.println("Enable Custom LUT");
  display->epd2.setCustomLUT(true);  // Enable custom LUT
  uint16_t calculated_time = display->epd2.getCustomLUTRefreshTime();
  Serial.printf("Calculated refresh time: %d ms\n", calculated_time);
}

void MenuDisplay::handleVolumeDown() {
  Serial.println("Disable Custom LUT");
  display->epd2.setCustomLUT(false);  // Disable custom LUT and reset to defaults
}

void MenuDisplay::handleConfirm() {
  // Toggle bebop image at bottom right (physical coords: 800-150=650, 480-200=280)
  bebopImageVisible = !bebopImageVisible;

  if (bebopImageVisible) {
    Serial.println("Showing bebop image...");
    display->drawImage(bebop_image, 800 - BEBOP_WIDTH - 10, 315, BEBOP_WIDTH, BEBOP_HEIGHT, false, false, true);
  } else {
    Serial.println("Hiding bebop image...");
    // Clear the image area by drawing white rectangle
    display->setPartialWindow(800 - BEBOP_WIDTH - 10, 315, BEBOP_WIDTH, BEBOP_HEIGHT);
    display->firstPage();
    do {
      display->fillScreen(GxEPD_WHITE);
    } while (display->nextPage());
  }
  Serial.println(bebopImageVisible ? "Image shown" : "Image hidden");
}
