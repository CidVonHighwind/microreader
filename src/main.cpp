#include <Arduino.h>
#include <esp_sleep.h>

#include "Buttons.h"
#include "DisplayController.h"
#include "EInkDisplay.h"

// USB detection pin
#define UART0_RXD 20  // Used for USB connection detection

// Power button timing
const unsigned long POWER_BUTTON_WAKEUP_MS = 500;  // Time required to confirm boot from sleep

// Display SPI pins (custom pins, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)
#define EPD_CS 21    // Chip Select
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy

Buttons buttons;
EInkDisplay einkDisplay(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
DisplayController displayController(einkDisplay);

// Check if USB is connected
bool isUsbConnected() {
  // U0RXD/GPIO20 reads HIGH when USB is connected
  return digitalRead(UART0_RXD) == HIGH;
}

// Verify long press on wake-up from deep sleep
void verifyWakeupLongPress() {
  const int POWER_BUTTON_PIN = 3;
  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);

  unsigned long pressStart = millis();
  bool abortBoot = false;

  // Monitor button state for the duration
  while (millis() - pressStart < POWER_BUTTON_WAKEUP_MS) {
    if (digitalRead(POWER_BUTTON_PIN) == HIGH) {
      abortBoot = true;
      break;
    }
    delay(10);
  }

  if (abortBoot) {
    Serial.println("Power button released too early. Returning to sleep.");
    // Re-arm the wakeup trigger before sleeping again
    esp_deep_sleep_enable_gpio_wakeup(1ULL << POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
  } else {
    Serial.println("Power button held long for " + String(millis() - pressStart) + " ms. Booting normally.");
  }
}

// Enter deep sleep mode
void enterDeepSleep() {
  const int POWER_BUTTON_PIN = 3;

  Serial.println("Power button long press detected. Entering deep sleep.");

  // Show sleep screen
  displayController.showSleepScreen();
  einkDisplay.displayBuffer(EInkDisplay::FULL_REFRESH);

  // Power off display and enter deep sleep mode
  einkDisplay.powerOff();

  // Enable wakeup on power button (active LOW)
  esp_deep_sleep_enable_gpio_wakeup(1ULL << POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);

  Serial.println("Entering deep sleep mode...");
  delay(10);  // Allow serial buffer to empty

  // Enter deep sleep
  esp_deep_sleep_start();
}

void setup() {
  // Check if boot was triggered by the power button (deep sleep wakeup)
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
    verifyWakeupLongPress();
  }

  // Configure USB detection pin
  pinMode(UART0_RXD, INPUT);

  Serial.begin(115200);

  // Only wait for serial monitor if USB is connected
  if (isUsbConnected()) {
    unsigned long start = millis();
    while (!Serial && (millis() - start) < 3000) {
      delay(10);
    }
  }

  Serial.println("\n=================================");
  Serial.println("  MicroReader - ESP32-C3 E-Ink");
  Serial.println("=================================");
  Serial.println();

  // Initialize buttons
  buttons.begin();
  Serial.println("Buttons initialized");

  // Initialize display driver (handles SPI, display init, and configuration)
  einkDisplay.begin();

  // Initialize display controller (handles application logic)
  displayController.begin();

  Serial.println("Initialization complete!\n");
}

void loop() {
  static unsigned long lastMemPrint = 0;

  buttons.update();

  // Print memory stats every second
  if (millis() - lastMemPrint >= 1000) {
    Serial.printf("[%lu] Memory - Free: %d bytes, Total: %d bytes, Min Free: %d bytes\n", millis(), ESP.getFreeHeap(),
                  ESP.getHeapSize(), ESP.getMinFreeHeap());
    lastMemPrint = millis();
  }

  // Check for power button press to enter sleep
  if (buttons.isPowerButtonPressed()) {
    // Wait for button release
    enterDeepSleep();
  }

  displayController.handleButtons(buttons);
}
