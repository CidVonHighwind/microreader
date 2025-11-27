#ifndef IMAGE_VIEWER_SCREEN_H
#define IMAGE_VIEWER_SCREEN_H

#include "EInkDisplay.h"
#include "Screen.h"

class ImageViewerScreen : public Screen {
 public:
  ImageViewerScreen(EInkDisplay& display);
  void show() override;

  void handleButtons(class Buttons& buttons) override;

 private:
  EInkDisplay& display;
  int index = 0;
};

#endif
