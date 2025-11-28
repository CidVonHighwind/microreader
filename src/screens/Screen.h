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
  virtual void activate() {
    show();
  }

  // Called when the device is powering down so the screen can persist state
  // Default implementation does nothing; override in screens that need to
  // save state (e.g. `TextViewerScreen` saving current position).
  virtual void shutdown() {}
};

#endif
