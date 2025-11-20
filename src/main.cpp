#include <Arduino.h>

#include "Buttons.h"
#include "CustomDisplay.h"
// #include "MenuDisplay.h"

// Display SPI pins (custom pins, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)
#define EPD_CS 21    // Chip Select
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy

Buttons buttons;
CustomDisplay display(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);

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

  // Initialize display driver (handles SPI, display init, and configuration)
  display.begin();

  Serial.println("Initialization complete!\n");
}

void loop() {
  buttons.update();

  if (buttons.wasPressed(Buttons::RIGHT)) {
    display.handleButton(CustomDisplay::RIGHT);
  } else if (buttons.wasPressed(Buttons::LEFT)) {
    display.handleButton(CustomDisplay::LEFT);
  } else if (buttons.wasPressed(Buttons::VOLUME_UP)) {
    display.handleButton(CustomDisplay::VOLUME_UP);
  } else if (buttons.wasPressed(Buttons::VOLUME_DOWN)) {
    display.handleButton(CustomDisplay::VOLUME_DOWN);
  } else if (buttons.wasPressed(Buttons::CONFIRM)) {
    display.handleButton(CustomDisplay::CONFIRM);
  } else if (buttons.wasPressed(Buttons::BACK)) {
    display.handleButton(CustomDisplay::BACK);
  }
}
