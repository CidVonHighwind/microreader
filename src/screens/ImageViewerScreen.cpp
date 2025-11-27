#include "screens/ImageViewerScreen.h"

#include <Arduino.h>

#include "../image/bebop_image.h"
#include "../image/test_image.h"
#include "Buttons.h"

static const int NUM_SCREENS = 4;

ImageViewerScreen::ImageViewerScreen(EInkDisplay& display, UIManager& uiManager)
    : display(display), uiManager(uiManager) {}

void ImageViewerScreen::handleButtons(Buttons& buttons) {
  if (buttons.wasPressed(Buttons::LEFT)) {
    index = (index - 1 + NUM_SCREENS) % NUM_SCREENS;
    show();
  } else if (buttons.wasPressed(Buttons::RIGHT)) {
    index = (index + 1) % NUM_SCREENS;
    show();
  } else if (buttons.wasPressed(Buttons::VOLUME_UP)) {
    uiManager.showScreen(UIManager::ScreenId::FileBrowser);
  } else if (buttons.wasPressed(Buttons::VOLUME_DOWN)) {
    display.refreshDisplay(EInkDisplay::FULL_REFRESH);
  } else if (buttons.wasPressed(Buttons::BACK)) {
    display.grayscaleRevert();
  }
}

void ImageViewerScreen::show() {
  switch (index % NUM_SCREENS) {
    case 0:
      Serial.printf("[%lu] ImageViewer: IMAGE 0\n", millis());
      display.setGrayscaleBuffers(test_image, test_image_lsb, test_image_msb);
      display.displayBuffer(EInkDisplay::FAST_REFRESH);
      break;
    case 1:
      Serial.printf("[%lu] ImageViewer: IMAGE 1\n", millis());
      display.setGrayscaleBuffers(bebop_image, bebop_image_lsb, bebop_image_msb);
      display.displayBuffer(EInkDisplay::FAST_REFRESH);
      break;
    case 2:
      Serial.printf("[%lu] ImageViewer: WHITE\n", millis());
      display.clearScreen(0xFF);
      display.displayBuffer(EInkDisplay::FAST_REFRESH);
      break;
    case 3:
      Serial.printf("[%lu] ImageViewer: BLACK\n", millis());
      display.clearScreen(0x00);
      display.displayBuffer(EInkDisplay::FAST_REFRESH);
      break;
  }
}