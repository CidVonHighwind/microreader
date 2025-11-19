#include <Arduino.h>
#include <EInk_Gray_Display.h>
// #include <EInk426_Gray.h>
#include <EInk426_BW.h>
#include <EInk_BW_Display.h>
#include <SPI.h>

#include "Buttons.h"
// #include "MenuDisplayGray.h"
#include "MenuDisplay.h"

// Display SPI pins (custom pins, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)
#define EPD_CS 21    // Chip Select
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy

// 4.26" BW display with GFX wrapper
EInk_BW_Display<EInk426_BW, EInk426_BW::HEIGHT> display(EInk426_BW(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

Buttons buttons;
MenuDisplay menuDisplay(display);
// MenuDisplayGray menuDisplay(display);

// Menu state
const char* menuItems[] = {"War and Peace",    "1984",      "To Kill a Mockingbird",  "Pride and Prejudice",
                           "The Great Gatsby", "Moby Dick", "The Catcher in the Rye", "Lord of the Rings",
                           "Harry Potter",     "The Hobbit"};

const int menuCount = 10;
int selectedIndex = 0;

// FreeRTOS task for non-blocking display updates
TaskHandle_t displayTaskHandle = NULL;
volatile bool fullRedrawRequested = false;

// Cursor tracking - volatile for thread safety
volatile int desiredCursorIndex = 0;    // Where cursor should be
volatile int displayedCursorIndex = 0;  // Where cursor currently is on screen

// Test rectangle state
bool testRectangleBlack = true;  // Toggle between black and white

// Display update task running on separate core
void displayUpdateTask(void* parameter) {
  while (1) {
    if (fullRedrawRequested) {
      int newIndex = desiredCursorIndex;
      menuDisplay.drawFullMenu(menuItems, menuCount, newIndex);
      displayedCursorIndex = newIndex;
      fullRedrawRequested = false;
    } else if (desiredCursorIndex != displayedCursorIndex) {
      int oldIndex = displayedCursorIndex;
      int newIndex = desiredCursorIndex;
      menuDisplay.updateCursor(menuItems, menuCount, oldIndex, newIndex);
      displayedCursorIndex = newIndex;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);

  // Wait for serial monitor
  unsigned long start = millis();
  while (!Serial && (millis() - start) < 3000) {
    delay(10);
  }

  Serial.println("\n=================================");
  Serial.println("  MicroReader - ESP32-C3 E-Ink");
  Serial.println("=================================");
  Serial.println();

  // Initialize buttons
  buttons.begin();
  Serial.println("Buttons initialized");

  // Initialize SPI with custom pins (SCLK=8, MOSI=10)
  // Default SPI.begin() is called by display.init(), but we need custom pins
  SPI.begin(EPD_SCLK, -1, EPD_MOSI, EPD_CS);

  // Initialize display (reset duration reduced to 1 for faster init)
  display.init(115200, true, 1, false);

  // Setup display text properties
  display.setRotation(3);
  display.setTextColor(GxEPD_BLACK);
  display.setTextSize(2);

  // Enable fast partial updates for quicker cursor movements (3-5x faster)
  // display.epd2.setFastPartialUpdate(true);

  Serial.println("Display initialized");

  // Initialize menu display
  menuDisplay.begin();

  // Draw initial menu
  menuDisplay.drawFullMenu(menuItems, menuCount, selectedIndex);

  desiredCursorIndex = selectedIndex;
  displayedCursorIndex = selectedIndex;

  // Create display update task on core 0 (main loop runs on core 1)
  xTaskCreatePinnedToCore(displayUpdateTask,   // Task function
                          "DisplayUpdate",     // Task name
                          4096,                // Stack size
                          NULL,                // Parameters
                          1,                   // Priority
                          &displayTaskHandle,  // Task handle
                          0                    // Core 0
  );

  Serial.println("Display task created");
  Serial.println("Initialization complete!\n");
}

void loop() {
  buttons.update();

  // Handle navigation - just update desired cursor position
  if (buttons.wasPressed(Buttons::RIGHT)) {
    selectedIndex--;
    if (selectedIndex < 0)
      selectedIndex = menuCount - 1;

    Serial.printf("Selected: %s\n", menuItems[selectedIndex]);
    desiredCursorIndex = selectedIndex;  // Display task will handle the update
  } else if (buttons.wasPressed(Buttons::LEFT)) {
    selectedIndex++;
    selectedIndex = selectedIndex % menuCount;

    Serial.printf("Selected: %s\n", menuItems[selectedIndex]);
    desiredCursorIndex = selectedIndex;  // Display task will handle the update
  } else if (buttons.wasPressed(Buttons::VOLUME_UP)) {
    Serial.println("Enable Custom LUT");
    display.epd2.setCustomLUT(true);  // Enable custom LUT
    uint16_t calculated_time = display.epd2.getCustomLUTRefreshTime();
    Serial.printf("Calculated refresh time: %d ms\n", calculated_time);
  } else if (buttons.wasPressed(Buttons::VOLUME_DOWN)) {
    Serial.println("Disable Custom LUT");
    display.epd2.setCustomLUT(false);  // Disable custom LUT and reset to defaults
  } else if (buttons.wasPressed(Buttons::CONFIRM)) {
    // Draw test rectangle with custom LUT using same pattern as cursor updates
    uint16_t fillColor = testRectangleBlack ? GxEPD_BLACK : GxEPD_WHITE;
    Serial.printf("Drawing test rectangle: %s\n", testRectangleBlack ? "BLACK" : "WHITE");

    // Use partial window and fill - same approach as updateCursor
    display.setPartialWindow(300, 30, 100, 100);
    display.firstPage();
    do {
      display.fillScreen(fillColor);
    } while (display.nextPage());

    testRectangleBlack = !testRectangleBlack;  // Toggle for next press
  } else if (buttons.wasPressed(Buttons::BACK)) {
    // Request full redraw in background task (this will clear the test rectangle)
    fullRedrawRequested = true;
  }
}
