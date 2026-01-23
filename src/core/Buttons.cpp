#include "Buttons.h"

const int Buttons::ADC_THRESHOLDS_1[] = {3470, 2655, 1470, 3};
const int Buttons::ADC_THRESHOLDS_2[] = {2205, 3};
const char* Buttons::BUTTON_NAMES[] = {"Back", "Confirm", "Left", "Right", "Volume Up", "Volume Down", "Power"};

Buttons::Buttons() : currentState(0), previousState(0), queueHead(0), queueTail(0), queueCount(0) {
  // Initialize per-button debounce state
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    lastButtonState[i] = 0;
    lastDebounceTime[i] = 0;
  }
  // Initialize press queue
  for (uint8_t i = 0; i < QUEUE_SIZE; i++) {
    pressQueue[i] = 0;
  }
}

void Buttons::begin() {
  pinMode(BUTTON_ADC_PIN_1, INPUT);
  pinMode(BUTTON_ADC_PIN_2, INPUT);
  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);
  analogSetAttenuation(ADC_11db);
}

int Buttons::getButtonFromADC(int adcValue, const int thresholds[], int numButtons) {
  if (adcValue > ADC_NO_BUTTON) {
    return -1;
  }

  for (int i = 0; i < numButtons; i++) {
    if (abs(adcValue - thresholds[i]) < ADC_TOLERANCE) {
      return i;
    }
  }

  return -1;
}

uint8_t Buttons::getState() {
  uint8_t state = 0;

  // Read GPIO1 buttons
  int adcValue1 = analogRead(BUTTON_ADC_PIN_1);
  int button1 = getButtonFromADC(adcValue1, ADC_THRESHOLDS_1, NUM_BUTTONS_1);
  if (button1 >= 0) {
    state |= (1 << button1);
  }

  // Read GPIO2 buttons
  int adcValue2 = analogRead(BUTTON_ADC_PIN_2);
  int button2 = getButtonFromADC(adcValue2, ADC_THRESHOLDS_2, NUM_BUTTONS_2);
  if (button2 >= 0) {
    state |= (1 << (button2 + 4));
  }

  // Read power button (digital, active LOW)
  if (digitalRead(POWER_BUTTON_PIN) == LOW) {
    state |= (1 << POWER);
  }

  return state;
}

void Buttons::update() {
  unsigned long currentTime = millis();
  uint8_t rawState = getState();

  // Save current state as previous before updating
  previousState = currentState;

  // Per-button debouncing (only for press, not release)
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    uint8_t buttonMask = (1 << i);
    uint8_t rawButtonState = (rawState & buttonMask) ? 1 : 0;
    uint8_t currentButtonState = (currentState & buttonMask) ? 1 : 0;
    uint8_t lastRawState = lastButtonState[i];

    // If raw state changed, reset debounce timer for this button
    if (rawButtonState != lastRawState) {
      lastDebounceTime[i] = currentTime;
      lastButtonState[i] = rawButtonState;
    }

    // Handle press with debounce, release immediately
    if (rawButtonState && !currentButtonState) {
      // Button is being pressed - wait for debounce
      if ((currentTime - lastDebounceTime[i]) > DEBOUNCE_DELAY) {
        currentState |= buttonMask;
        // Queue this press for consumption by screens
        enqueuePress(i);
      }
    } else if (!rawButtonState && currentButtonState) {
      // Button is being released - update immediately
      currentState &= ~buttonMask;
    }
  }
}

bool Buttons::isDown(uint8_t buttonIndex) {
  return currentState & (1 << buttonIndex);
}

bool Buttons::isPressed(uint8_t buttonIndex) {
  uint8_t mask = (1 << buttonIndex);
  return (currentState & mask) && !(previousState & mask);
}

bool Buttons::wasDown(uint8_t buttonIndex) {
  return previousState & (1 << buttonIndex);
}

bool Buttons::wasReleased(uint8_t buttonIndex) {
  uint8_t mask = (1 << buttonIndex);
  return !(currentState & mask) && (previousState & mask);
}

const char* Buttons::getButtonName(uint8_t buttonIndex) {
  if (buttonIndex <= POWER) {
    return BUTTON_NAMES[buttonIndex];
  }
  return "Unknown";
}

bool Buttons::isPowerButtonDown() {
  return isDown(POWER);
}

unsigned long Buttons::getHoldDuration(uint8_t buttonIndex) {
  if (!isDown(buttonIndex)) {
    return 0;  // Button not held
  }
  return millis() - lastDebounceTime[buttonIndex];
}

void Buttons::enqueuePress(uint8_t buttonIndex) {
  if (queueCount < QUEUE_SIZE) {
    pressQueue[queueHead] = buttonIndex;
    queueHead = (queueHead + 1) % QUEUE_SIZE;
    queueCount++;
  }
  // If queue is full, oldest presses are lost (acceptable for UI)
}

uint8_t Buttons::consumeNextPress() {
  if (queueCount == 0) {
    return NONE;
  }
  uint8_t button = pressQueue[queueTail];
  queueTail = (queueTail + 1) % QUEUE_SIZE;
  queueCount--;
  return button;
}

void Buttons::clearQueuedPresses() {
  queueHead = 0;
  queueTail = 0;
  queueCount = 0;
}
