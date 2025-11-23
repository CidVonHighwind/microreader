#ifndef SCREEN_H
#define SCREEN_H

#include "Buttons.h"

class Screen {
 public:
  virtual ~Screen() {}

  // Optional initialization for screens that need it
  virtual void begin() {}

  // Handle input; must be implemented by concrete screens
  virtual void handleButtons(Buttons& buttons) = 0;

  // Called when the screen should render itself (no args for generic screens)
  virtual void show() = 0;

  // Called when the screen becomes active; default forwards to show
  virtual void activate(int context = 0) {
    show();
  }
};

#endif
