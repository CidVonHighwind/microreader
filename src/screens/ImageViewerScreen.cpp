#include "screens/ImageViewerScreen.h"

#include <Arduino.h>

#include "../image/bebop_2.h"
#include "../image/bebop_image.h"
#include "Buttons.h"

ImageViewerScreen::ImageViewerScreen(EInkDisplay& display) : display(display) {}

void ImageViewerScreen::handleButtons(Buttons& buttons) {
  if (buttons.wasPressed(Buttons::LEFT)) {
    index = (index - 1 + 4) % 4;
    show();
  } else if (buttons.wasPressed(Buttons::RIGHT)) {
    index = (index + 1) % 4;
    show();
  }
}

void ImageViewerScreen::show() {
  switch (index % 4) {
    case 0:
      Serial.printf("[%lu] ImageViewer: IMAGE 1\n", millis());
      display.drawImage(bebop_image, 0, 0, BEBOP_IMAGE_WIDTH, BEBOP_IMAGE_HEIGHT, true);
      break;
    case 1:
      Serial.printf("[%lu] ImageViewer: IMAGE 2\n", millis());
      display.drawImage(bebop_2, 0, 0, BEBOP_2_WIDTH, BEBOP_2_HEIGHT, true);
      break;
    case 2:
      Serial.printf("[%lu] ImageViewer: WHITE\n", millis());
      display.clearScreen(0xFF);
      break;
    case 3:
      Serial.printf("[%lu] ImageViewer: BLACK\n", millis());
      display.clearScreen(0x00);
      break;
  }

  display.displayBuffer(EInkDisplay::FAST_REFRESH);
}