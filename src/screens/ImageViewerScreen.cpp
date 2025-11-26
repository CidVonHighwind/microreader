#include "screens/ImageViewerScreen.h"

#include <Arduino.h>

#include "../image/bebop_image.h"
#include "../image/test04_image.h"
#include "../image/test05_image.h"
#include "../image/test_image.h"
#include "Buttons.h"

ImageViewerScreen::ImageViewerScreen(EInkDisplay& display) : display(display) {}

void ImageViewerScreen::handleButtons(Buttons& buttons) {
  if (buttons.wasPressed(Buttons::LEFT)) {
    index = (index - 1 + NUM_SCREENS) % NUM_SCREENS;
    show();
  } else if (buttons.wasPressed(Buttons::RIGHT)) {
    index = (index + 1) % NUM_SCREENS;
    show();
  }
}

void ImageViewerScreen::show() {
  switch (index % NUM_SCREENS) {
    case 0:
      Serial.printf("[%lu] ImageViewer: IMAGE 0\n", millis());
      display.drawImage(test_image, 0, 0, TEST_IMAGE_WIDTH, TEST_IMAGE_HEIGHT, true);
      display.displayBuffer(EInkDisplay::FAST_REFRESH);
      break;
    case 1:
      Serial.printf("[%lu] ImageViewer: IMAGE 1\n", millis());
      display.drawImage(test_image, 0, 0, TEST_IMAGE_WIDTH, TEST_IMAGE_HEIGHT, true);
      display.displayBuffer(EInkDisplay::FAST_REFRESH);
      display.displayBufferGrayscale(test_image_lsb, test_image_msb, test_image);
      break;
    case 2:
      Serial.printf("[%lu] ImageViewer: IMAGE 2\n", millis());
      display.drawImage(test04_image, 0, 0, TEST_IMAGE_WIDTH, TEST_IMAGE_HEIGHT, true);
      display.displayBuffer(EInkDisplay::FAST_REFRESH);
      display.displayBufferGrayscale(test04_image_lsb, test04_image_msb, test04_image);
      break;
    case 3:
      Serial.printf("[%lu] ImageViewer: IMAGE 3\n", millis());
      display.drawImage(test05_image, 0, 0, TEST_IMAGE_WIDTH, TEST_IMAGE_HEIGHT, true);
      display.displayBuffer(EInkDisplay::FAST_REFRESH);
      display.displayBufferGrayscale(test05_image_lsb, test05_image_msb, test05_image);
      break;
    case 4:
      Serial.printf("[%lu] ImageViewer: IMAGE 4\n", millis());
      display.drawImage(bebop_image, 0, 0, BEBOP_IMAGE_WIDTH, BEBOP_IMAGE_HEIGHT, true);
      display.displayBuffer(EInkDisplay::FAST_REFRESH);
      break;
    case 5:
      Serial.printf("[%lu] ImageViewer: WHITE\n", millis());
      display.clearScreen(0xFF);
      display.displayBuffer(EInkDisplay::FAST_REFRESH);
      break;
    case 6:
      Serial.printf("[%lu] ImageViewer: BLACK\n", millis());
      display.clearScreen(0x00);
      display.displayBuffer(EInkDisplay::FAST_REFRESH);
      break;
  }
}