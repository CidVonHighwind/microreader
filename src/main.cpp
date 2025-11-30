#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <esp_sleep.h>

#include "BatteryMonitor.h"
#include "Buttons.h"
#include "EInkDisplay.h"
#include "SDCardManager.h"
#include "UIManager.h"

// USB detection pin
#define UART0_RXD 20  // Used for USB connection detection

// Power button timing
const unsigned long POWER_BUTTON_WAKEUP_MS = 250;  // Time required to confirm boot from sleep
// Power button pin (used in multiple places)
const int POWER_BUTTON_PIN = 3;

// Display SPI pins (custom pins, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)

#define SD_SPI_CS 12  // SD Card Chip Select
#define SD_SPI_MISO 7

#define EINK_SPI_CS 21  // EINK Chip Select

Buttons buttons;
EInkDisplay einkDisplay(EPD_SCLK, EPD_MOSI, EINK_SPI_CS, EPD_DC, EPD_RST, EPD_BUSY);
SDCardManager sdManager(EPD_SCLK, SD_SPI_MISO, EPD_MOSI, SD_SPI_CS, EINK_SPI_CS);
// Battery ADC pin and global instance
#define BAT_GPIO0 0
BatteryMonitor g_battery(BAT_GPIO0);
UIManager uiManager(einkDisplay, sdManager);

// Helper to turn wakeup cause into a readable string for debugging
const char* wakeupCauseToString(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      return "UNDEFINED";
    case ESP_SLEEP_WAKEUP_EXT0:
      return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1:
      return "EXT1";
    case ESP_SLEEP_WAKEUP_TIMER:
      return "TIMER";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      return "TOUCHPAD";
    case ESP_SLEEP_WAKEUP_ULP:
      return "ULP";
    case ESP_SLEEP_WAKEUP_GPIO:
      return "GPIO";
    default:
      return "OTHER";
  }
}

// Check if USB is connected
bool isUsbConnected() {
  // U0RXD/GPIO20 reads HIGH when USB is connected
  return digitalRead(UART0_RXD) == HIGH;
}

// Verify long press on wake-up
void verifyWakeupLongPress() {
  unsigned long timerStart = millis();
  long pressDuration = 0;
  bool bootDevice = false;

  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);

  // Monitor button state for the duration
  while (millis() - timerStart < POWER_BUTTON_WAKEUP_MS * 2) {
    delay(10);
    if (digitalRead(POWER_BUTTON_PIN) == LOW) {
      pressDuration += 10;

      if (pressDuration >= POWER_BUTTON_WAKEUP_MS) {
        // Long press detected; normal boot
        bootDevice = true;
        break;
      }
    } else {
      // Button released; reset timer
      pressDuration = 0;
    }
  }

  if (!bootDevice) {
    // Enable wakeup on power button (active LOW)
    pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
  }
}

// Enter deep sleep mode
void enterDeepSleep() {
  Serial.println("Power button long press detected. Entering deep sleep.");

  // Let UI save any persistent state before we render the sleep screen
  uiManager.prepareForSleep();

  // Show sleep screen
  uiManager.showSleepScreen();

  // Enter deep sleep mode
  einkDisplay.deepSleep();

  Serial.println("Entering deep sleep mode...");
  delay(10);  // Allow serial buffer to empty

  // Enable wakeup on power button (active LOW)
  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
}

void setup() {
  // Only start/wait for serial monitor if USB is connected
  pinMode(UART0_RXD, INPUT);
  if (isUsbConnected()) {
    Serial.begin(115200);

    unsigned long start = millis();
    while (!Serial && (millis() - start) < 3000) {
      delay(10);
    }
  } else {
    verifyWakeupLongPress();
  }

  Serial.println("\n=================================");
  Serial.println("  MicroReader - ESP32-C3 E-Ink");
  Serial.println("=================================");
  Serial.println();

  // Initialize buttons
  buttons.begin();
  Serial.println("Buttons initialized");

  // Initialize SD card manager
  sdManager.begin();

  {
    esp_sleep_wakeup_cause_t w = esp_sleep_get_wakeup_cause();
    String dbg = String("wakeup: ") + wakeupCauseToString(w) + " (" + String((int)w) + ")\n";
    dbg += String("power_raw: ") + String(digitalRead(POWER_BUTTON_PIN)) + "\n";
    // Try to write; writeFile removes existing file then writes new content
    sdManager.writeFile("/log.txt", dbg);

    if (sdManager.ready()) {
      if (!sdManager.writeFile("/log.txt", dbg)) {
        Serial.println("UIManager: Failed to write debug.txt to SD");
      }
    } else {
      Serial.println("UIManager: SD not ready; skipping debug log write");
    }
  }

  // Initialize display driver (handles SPI, display init, and configuration)
  einkDisplay.begin();

  // Initialize display controller (handles application logic)
  uiManager.begin();

  Serial.println("Initialization complete!\n");
}

void loop() {
  // Print memory stats every second
  static unsigned long lastMemPrint = 0;
  if (Serial && millis() - lastMemPrint >= 1000) {
    Serial.printf("[%lu] Memory - Free: %d bytes, Total: %d bytes, Min Free: %d bytes\n", millis(), ESP.getFreeHeap(),
                  ESP.getHeapSize(), ESP.getMinFreeHeap());
    lastMemPrint = millis();
  }

  buttons.update();

  uiManager.handleButtons(buttons);

  // Check for power button press to enter sleep
  if (buttons.isPowerButtonPressed()) {
    enterDeepSleep();
  }

  // Small delay to avoid busy loop
  delay(10);
}
