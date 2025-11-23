#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>

class Buttons {
 public:
  Buttons();
  void begin();
  uint8_t getState();
  void update();
  bool isPressed(uint8_t buttonIndex);
  bool wasPressed(uint8_t buttonIndex);
  bool wasReleased(uint8_t buttonIndex);

  // Button indices
  static const uint8_t BACK = 0;
  static const uint8_t CONFIRM = 1;
  static const uint8_t LEFT = 2;
  static const uint8_t RIGHT = 3;
  static const uint8_t VOLUME_UP = 4;
  static const uint8_t VOLUME_DOWN = 5;
  static const uint8_t POWER = 6;

  // Power button methods
  bool isPowerButtonPressed();
  unsigned long getPowerButtonPressDuration();

  // Button names
  static const char* getButtonName(uint8_t buttonIndex);

 private:
  int getButtonFromADC(int adcValue, const int thresholds[], int numButtons);

  uint8_t currentState;
  uint8_t lastState;
  uint8_t pressedEvents;
  uint8_t releasedEvents;
  unsigned long lastDebounceTime;
  unsigned long powerButtonPressStart;
  bool powerButtonWasPressed;

  static const int BUTTON_ADC_PIN_1 = 1;
  static const int NUM_BUTTONS_1 = 4;
  static const int ADC_THRESHOLDS_1[];

  static const int POWER_BUTTON_PIN = 3;

  static const int BUTTON_ADC_PIN_2 = 2;
  static const int NUM_BUTTONS_2 = 2;
  static const int ADC_THRESHOLDS_2[];

  static const int ADC_TOLERANCE = 200;
  static const int ADC_NO_BUTTON = 3800;
  static const unsigned long DEBOUNCE_DELAY = 5;

  static const char* BUTTON_NAMES[];
};

#endif
