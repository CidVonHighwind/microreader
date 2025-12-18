#include "UIManager.h"

#include <resources/fonts/FontManager.h>

#include "core/Settings.h"
#include "resources/images/bebop_image.h"
#include "ui/screens/FileBrowserScreen.h"
#include "ui/screens/ImageViewerScreen.h"
#include "ui/screens/TextViewerScreen.h"

UIManager::UIManager(EInkDisplay& display, SDCardManager& sdManager)
    : display(display), sdManager(sdManager), textRenderer(display) {
  // Initialize consolidated settings manager
  settings = new Settings(sdManager);
  // Create concrete screens and store pointers in the map.
  screens[ScreenId::FileBrowser] =
      std::unique_ptr<Screen>(new FileBrowserScreen(display, textRenderer, sdManager, *this));
  screens[ScreenId::ImageViewer] = std::unique_ptr<Screen>(new ImageViewerScreen(display, *this));
  screens[ScreenId::TextViewer] =
      std::unique_ptr<Screen>(new TextViewerScreen(display, textRenderer, sdManager, *this));
  Serial.printf("[%lu] UIManager: Constructor called\n", millis());
}

UIManager::~UIManager() {
  if (settings)
    delete settings;
}

void UIManager::begin() {
  Serial.printf("[%lu] UIManager: begin() called\n", millis());
  // Load consolidated settings (import legacy files on first run)
  if (sdManager.ready()) {
    if (settings)
      settings->load();
  }
  // Initialize screens using generic Screen interface
  for (auto& p : screens) {
    if (p.second)
      p.second->begin();
  }

  // Restore last-visible screen (use consolidated settings when available)
  currentScreen = ScreenId::FileBrowser;
  if (sdManager.ready() && settings) {
    int saved = 0;
    if (settings->getInt(String("ui.screen"), saved)) {
      if (saved >= 0 && saved <= static_cast<int>(ScreenId::TextViewer)) {
        currentScreen = static_cast<ScreenId>(saved);
        Serial.printf("[%lu] UIManager: Restored screen %d from settings\n", millis(), saved);
      } else {
        Serial.printf("[%lu] UIManager: Invalid saved screen %d; using default\n", millis(), saved);
      }
    } else {
      Serial.printf("[%lu] UIManager: No saved screen state found; using default\n", millis());
    }
  } else {
    Serial.printf("[%lu] UIManager: SD not ready; using default start screen\n", millis());
  }

  showScreen(currentScreen);

  Serial.printf("[%lu] UIManager initialized\n", millis());
}

void UIManager::handleButtons(Buttons& buttons) {
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
  {
    textRenderer.setFrameBuffer(display.getFrameBuffer());
    textRenderer.setBitmapType(TextRenderer::BITMAP_BW);
    textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
    textRenderer.setFont(getMainFont());

    const char* sleepText = "Sleeping...";
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(sleepText, 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (480 - w) / 2;

    textRenderer.setCursor(centerX, 780);
    textRenderer.print(sleepText);
  }

  // show the image with the grayscale antialiasing
  display.displayBuffer(EInkDisplay::FULL_REFRESH);
  display.copyGrayscaleBuffers(bebop_image_lsb, bebop_image_msb);
  display.displayGrayBuffer(true);
}

void UIManager::prepareForSleep() {
  // Notify the active screen that the device is powering down so it can
  // persist any state (e.g. current reading position).
  if (screens[currentScreen])
    screens[currentScreen]->shutdown();
  // Persist which screen was active so we can restore it on next boot.
  if (sdManager.ready() && settings) {
    settings->setInt(String("ui.screen"), static_cast<int>(currentScreen));
    if (!settings->save()) {
      Serial.println("UIManager: Failed to write settings.cfg to SD");
    }
  } else {
    Serial.println("UIManager: SD not ready; skipping save of current screen");
  }
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
  // Call activate so screens can perform any work needed when they become
  // active (this also ensures TextViewerScreen::activate is invoked to open
  // any pending file that was loaded during begin()).
  screens[id]->activate();
  screens[id]->show();
}
