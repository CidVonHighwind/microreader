#ifndef DISPLAY_CONTROLLER_H
#define DISPLAY_CONTROLLER_H

#include "Buttons.h"
#include "EInkDisplay.h"

class DisplayController {
 public:
  // Constructor
  DisplayController(EInkDisplay& display);

  // Initialize and show startup screen
  void begin();

  // Handle button presses
  void handleButtons(Buttons& buttons);

 private:
  EInkDisplay& display;
};

#endif
