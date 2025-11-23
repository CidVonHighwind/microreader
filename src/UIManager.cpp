#include "UIManager.h"

#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>

#include "image/bebop_image.h"
#include "screens/FileBrowserScreen.h"
#include "screens/ImageViewerScreen.h"
#include "screens/TextViewerScreen.h"

UIManager::UIManager(EInkDisplay& display, SDCardManager& sdManager)
    : display(display), sdManager(sdManager), textRenderer(display), textLayout(), currentScreenIndex(0) {
  screens.emplace_back(std::unique_ptr<FileBrowserScreen>(new FileBrowserScreen(display, textRenderer, sdManager)));
  screens.emplace_back(std::unique_ptr<ImageViewerScreen>(new ImageViewerScreen(display)));
  screens.emplace_back(std::unique_ptr<TextViewerScreen>(new TextViewerScreen(display, textRenderer)));
  Serial.printf("[%lu] UIManager: Constructor called\n", millis());
}

void UIManager::begin() {
  Serial.printf("[%lu] UIManager: begin() called\n", millis());
  // Initialize screens using generic Screen interface
  for (auto& s : screens) {
    s->begin();
  }

  // Show starting screen
  showScreen(currentScreenIndex);

  display.displayBuffer(EInkDisplay::HALF_REFRESH);
  Serial.printf("[%lu] UIManager initialized\n", millis());
}

void UIManager::showScreen(int index) {
  currentScreenIndex = index;
  if (index >= 0 && index < (int)screens.size()) {
    screens[index]->show();
  }
}

void UIManager::handleButtons(Buttons& buttons) {
  if (buttons.wasPressed(Buttons::VOLUME_UP) || buttons.wasPressed(Buttons::VOLUME_DOWN)) {
    // Move through the screens vector: UP => previous, DOWN => next (wrap-around).
    if (buttons.wasPressed(Buttons::VOLUME_UP)) {
      int n = (int)screens.size();
      currentScreenIndex = (currentScreenIndex - 1 + n) % n;
    } else {  // VOLUME_DOWN
      currentScreenIndex = (currentScreenIndex + 1) % (int)screens.size();
    }

    screens[currentScreenIndex]->show();
    return;
  }

  // Pass buttons to the current screen
  if (currentScreenIndex >= 0 && currentScreenIndex < (int)screens.size())
    screens[currentScreenIndex]->handleButtons(buttons);
}

void UIManager::showSleepScreen() {
  Serial.printf("[%lu] Showing SLEEP screen\n", millis());
  display.clearScreen(0xFF);

  // Draw bebop image centered
  display.drawImage(bebop_image, 0, 0, BEBOP_IMAGE_WIDTH, BEBOP_IMAGE_HEIGHT, true);

  // Add "Sleeping..." text at the bottom
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFont(&FreeSans12pt7b);

  const char* sleepText = "Sleeping...";
  int16_t x1, y1;
  uint16_t w, h;
  textRenderer.getTextBounds(sleepText, 0, 0, &x1, &y1, &w, &h);
  int16_t centerX = (480 - w) / 2;

  textRenderer.setCursor(centerX, 780);
  textRenderer.print(sleepText);
}
