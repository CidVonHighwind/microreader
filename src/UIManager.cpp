#include "UIManager.h"

#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>

#include "image/bebop_image.h"
#include "screens/FileBrowserScreen.h"
#include "screens/ImageViewerScreen.h"
#include "screens/TextViewerScreen.h"

UIManager::UIManager(EInkDisplay& display, SDCardManager& sdManager)
    : display(display), sdManager(sdManager), textRenderer(display), textLayout() {
  // Create concrete screens and store pointers in the map.
  screens[ScreenId::FileBrowser] =
      std::unique_ptr<Screen>(new FileBrowserScreen(display, textRenderer, sdManager, *this));
  screens[ScreenId::ImageViewer] = std::unique_ptr<Screen>(new ImageViewerScreen(display));
  screens[ScreenId::TextViewer] = std::unique_ptr<Screen>(new TextViewerScreen(display, textRenderer, sdManager));
  Serial.printf("[%lu] UIManager: Constructor called\n", millis());
}

void UIManager::begin() {
  Serial.printf("[%lu] UIManager: begin() called\n", millis());
  // Initialize screens using generic Screen interface
  for (auto& p : screens) {
    if (p.second)
      p.second->begin();
  }

  // Show starting screen (FileBrowser)
  currentScreen = ScreenId::FileBrowser;
  showScreen(currentScreen);

  display.displayBuffer(EInkDisplay::HALF_REFRESH);
  Serial.printf("[%lu] UIManager initialized\n", millis());
}

// navigation helpers (small fixed set; implement by scanning enum list)
UIManager::ScreenId UIManager::nextScreenId(UIManager::ScreenId cur) const {
  auto it = std::find(screenOrder.begin(), screenOrder.end(), cur);
  if (it != screenOrder.end()) {
    ++it;
    if (it == screenOrder.end())
      it = screenOrder.begin();
    return *it;
  }
  return screenOrder[0];
}

UIManager::ScreenId UIManager::prevScreenId(UIManager::ScreenId cur) const {
  auto it = std::find(screenOrder.begin(), screenOrder.end(), cur);
  if (it != screenOrder.end()) {
    if (it == screenOrder.begin())
      it = screenOrder.end();
    --it;
    return *it;
  }
  return screenOrder[0];
}

void UIManager::handleButtons(Buttons& buttons) {
  if (buttons.wasPressed(Buttons::VOLUME_UP) || buttons.wasPressed(Buttons::VOLUME_DOWN)) {
    // Move through the screens vector: UP => previous, DOWN => next (wrap-around).
    if (buttons.wasPressed(Buttons::VOLUME_UP)) {
      currentScreen = prevScreenId(currentScreen);
    } else {  // VOLUME_DOWN
      currentScreen = nextScreenId(currentScreen);
    }

    // We guarantee the screen exists, so access directly.
    screens[currentScreen]->show();
    return;
  }

  // Pass buttons to the current screen
  // Directly forward to the active screen (must exist)
  screens[currentScreen]->handleButtons(buttons);
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

void UIManager::openTextFile(const String& sdPath) {
  Serial.printf("UIManager: openTextFile %s\n", sdPath.c_str());
  // Directly access TextViewerScreen and open the file (guaranteed to exist)
  static_cast<TextViewerScreen*>(screens[ScreenId::TextViewer].get())->openFile(sdPath);
  showScreen(ScreenId::TextViewer);
}

void UIManager::showScreen(ScreenId id) {
  // Directly show the requested screen (assumed present)
  currentScreen = id;
  screens[id]->show();
}
