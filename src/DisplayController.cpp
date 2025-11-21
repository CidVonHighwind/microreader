#include "DisplayController.h"

#include "bebop_image.h"

DisplayController::DisplayController(EInkDisplay& display) : display(display) {
  Serial.printf("[%lu] DisplayController: Constructor called\n", millis());
}

void DisplayController::begin() {
  Serial.printf("[%lu] DisplayController: begin() called\n", millis());

  // Display bebop image on startup
  display.drawImage(bebop_image, 0, 0, BEBOP_WIDTH, BEBOP_HEIGHT, true);
  display.displayBuffer(true);  // Full refresh

  Serial.printf("[%lu] DisplayController initialized\n", millis());
}

void DisplayController::handleButtons(Buttons& buttons) {
  if (buttons.wasPressed(Buttons::VOLUME_UP)) {
    display.setCustomLUT(true);
  } else if (buttons.wasPressed(Buttons::VOLUME_DOWN)) {
    display.setCustomLUT(false);
  } else if (buttons.wasPressed(Buttons::CONFIRM)) {
    Serial.printf("[%lu] Displaying bebop image...\n", millis());
    display.drawImage(bebop_image, 0, 0, BEBOP_WIDTH, BEBOP_HEIGHT, true);
    display.displayBuffer(false);  // Partial refresh
  } else if (buttons.wasPressed(Buttons::BACK)) {
    display.displayBuffer(true);  // Full refresh
  } else if (buttons.wasPressed(Buttons::LEFT)) {
    Serial.printf("[%lu] Clearing screen to WHITE\n", millis());
    display.clearScreen(0xFF);
    display.displayBuffer(false);  // Partial refresh
  } else if (buttons.wasPressed(Buttons::RIGHT)) {
    Serial.printf("[%lu] Clearing screen to BLACK\n", millis());
    display.clearScreen(0x00);
    display.displayBuffer(false);  // Partial refresh
  }
}
