
#include "FileBrowserScreen.h"

#include <resources/fonts/FontDefinitions.h>
#include <resources/fonts/FontManager.h>
#include <resources/fonts/other/MenuFontBig.h>
#include <resources/fonts/other/MenuFontSmall.h>
#include <resources/fonts/other/MenuHeader.h>

#include <algorithm>
#include <cstring>

#include "../../core/BatteryMonitor.h"
#include "../../core/Buttons.h"
#include "../../core/Settings.h"
#include "../UIManager.h"

FileBrowserScreen::FileBrowserScreen(EInkDisplay& display, TextRenderer& renderer, SDCardManager& sdManager,
                                     UIManager& uiManager)
    : display(display), textRenderer(renderer), sdManager(sdManager), uiManager(uiManager) {}

void FileBrowserScreen::begin() {
  loadFolder();
}

// Ensure member function is in class scope
void FileBrowserScreen::handleButtons(Buttons& buttons) {
  bool needsUpdate = false;
  bool shouldGoBack = false;
  bool shouldConfirm = false;

  // Consume all queued button presses
  uint8_t btn;
  while ((btn = buttons.consumeNextPress()) != Buttons::NONE) {
    switch (btn) {
      case Buttons::BACK:
        shouldGoBack = true;
        break;

      case Buttons::CONFIRM:
        shouldConfirm = true;
        break;

      case Buttons::LEFT:
        selectNext();
        needsUpdate = true;
        break;

      case Buttons::RIGHT:
        selectPrev();
        needsUpdate = true;
        break;
    }
  }

  // Handle navigation after processing all inputs
  if (shouldGoBack) {
    uiManager.showScreen(UIManager::ScreenId::Settings);
    return;
  }

  if (shouldConfirm) {
    confirm();
    return;
  }

  // Only update display once after processing all inputs
  if (needsUpdate) {
    show();
  }
}

void FileBrowserScreen::activate() {
  // Load and apply UI font settings
  Settings& s = uiManager.getSettings();
  int uiFontSize = 0;
  if (s.getInt(String("settings.uiFontSize"), uiFontSize)) {
    if (uiFontSize == 0) {
      setMainFont(&MenuFontSmall);
      setTitleFont(&MenuHeader);
    } else {
      setMainFont(&MenuFontBig);
      setTitleFont(&MenuHeader);
    }
  }

  loadFolder();
}

void FileBrowserScreen::show() {
  renderSdBrowser();
  display.displayBuffer(EInkDisplay::FAST_REFRESH);
}

void FileBrowserScreen::renderSdBrowser() {
  display.clearScreen(0xFF);
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFont(getTitleFont());

  // Set framebuffer to BW buffer for rendering
  textRenderer.setFrameBuffer(display.getFrameBuffer());
  textRenderer.setBitmapType(TextRenderer::BITMAP_BW);

  // Center the title horizontally (page width is 480 in portrait coordinate system)
  {
    const char* title = "Microreader";
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (480 - (int)w) / 2;
    textRenderer.setCursor(centerX, 75);
    textRenderer.print(title);
  }

  textRenderer.setFont(getMainFont());

  // Render file list centered both horizontally and vertically.
  textRenderer.setFont(getMainFont());
  const int lineHeight = 28;
  int lines = SD_LINES_PER_SCREEN;

  // Count how many actual rows we'll draw (clamped by available files)
  int drawable = 0;
  for (int i = 0; i < lines; ++i) {
    if (sdScrollOffset + i >= (int)sdFiles.size())
      break;
    ++drawable;
  }

  if (drawable == 0)
    return;

  // Get text height for proper vertical centering (Y is baseline, not top)
  int16_t tx1, ty1;
  uint16_t tw, textHeight;
  textRenderer.getTextBounds("Ag", 0, 0, &tx1, &ty1, &tw, &textHeight);  // Use sample text to get height

  int totalHeight = drawable * lineHeight;
  int startY = (800 - totalHeight) / 2 + textHeight;  // Add text height since Y is baseline

  for (int i = 0; i < drawable; ++i) {
    int idx = sdScrollOffset + i;
    String name = sdFiles[idx];
    // For display, strip the .txt extension if present but keep the stored
    // filename intact so confirm() can open it later.
    // For .epub files, keep the extension visible.
    String displayNameRaw = name;
    // Strip .epub extension (5 characters)
    if (displayNameRaw.length() >= 5) {
      String ext = displayNameRaw.substring(displayNameRaw.length() - 5);
      ext.toLowerCase();
      if (ext == String(".epub")) {
        displayNameRaw = displayNameRaw.substring(0, displayNameRaw.length() - 5);
      }
    }
    // Strip .txt extension (4 characters)
    if (displayNameRaw.length() >= 4) {
      String ext = displayNameRaw.substring(displayNameRaw.length() - 4);
      ext.toLowerCase();
      if (ext == String(".txt")) {
        displayNameRaw = displayNameRaw.substring(0, displayNameRaw.length() - 4);
      }
    }

    if (displayNameRaw.length() > 30)
      displayNameRaw = displayNameRaw.substring(0, 27) + "...";

    String displayName = displayNameRaw;

    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(displayName.c_str(), 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (480 - (int)w) / 2;
    int16_t rowY = startY + i * lineHeight;

    // Draw inverted selection bar for selected item
    if (idx == sdSelectedIndex) {
      int16_t barPadding = 4;                                   // Horizontal padding on each side
      int16_t barHeight = h + 4;                                // Slightly larger than text height
      int16_t barY = rowY - h + 1;                              // Align with text
      int16_t barX = centerX - barPadding;                      // Start slightly before text
      int16_t barWidth = w + barPadding * 2;                    // Text width plus padding
      display.fillRect(barX, barY, barWidth, barHeight, 0x00);  // Black bar behind text
      textRenderer.setTextColor(TextRenderer::COLOR_WHITE);     // White text
    }

    textRenderer.setCursor(centerX, rowY);
    textRenderer.print(displayName);

    // Reset to black text for non-selected items
    if (idx == sdSelectedIndex) {
      textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
    }
  }

  // Draw battery percentage at bottom-right of the screen
  {
    textRenderer.setFont(&MenuFontSmall);  // Always use small font for battery
    int pct = g_battery.readPercentage();
    String pctStr = String(pct) + "%";
    int16_t bx1, by1;
    uint16_t bw, bh;
    textRenderer.getTextBounds(pctStr.c_str(), 0, 0, &bx1, &by1, &bw, &bh);
    int16_t bx = (480 - (int)bw) / 2;
    // Use baseline near bottom (page height is 800); align similar to other screens
    int16_t by = 790;
    textRenderer.setCursor(bx, by);
    textRenderer.print(pctStr);
  }
}

void FileBrowserScreen::confirm() {
  if (!sdFiles.empty()) {
    String filename = sdFiles[sdSelectedIndex];
    String fullPath = String("/") + filename;
    Serial.printf("Selected file: %s\n", fullPath.c_str());

    // Ask UI manager to open the selected file in the text viewer
    uiManager.openTextFile(fullPath);
  }
}

void FileBrowserScreen::selectNext() {
  offsetSelection(1);
}

void FileBrowserScreen::selectPrev() {
  offsetSelection(-1);
}

void FileBrowserScreen::offsetSelection(int offset) {
  if (sdFiles.empty())
    return;

  int n = (int)sdFiles.size();
  int newIndex = sdSelectedIndex + offset;
  newIndex %= n;
  if (newIndex < 0)
    newIndex += n;
  sdSelectedIndex = newIndex;

  if (sdSelectedIndex >= sdScrollOffset + SD_LINES_PER_SCREEN) {
    sdScrollOffset = sdSelectedIndex - SD_LINES_PER_SCREEN + 1;
  } else if (sdSelectedIndex < sdScrollOffset) {
    sdScrollOffset = sdSelectedIndex;
  }

  // Persist the current selection into consolidated settings
  if (!sdFiles.empty()) {
    Settings& s = uiManager.getSettings();
    s.setString(String("filebrowser.selected"), sdFiles[sdSelectedIndex]);
  }
}

void FileBrowserScreen::loadFolder(int maxFiles) {
  sdFiles.clear();

  if (!sdManager.ready()) {
    Serial.println("SD not ready; cannot list files.");
    return;
  }

  auto files = sdManager.listFiles("/", maxFiles);
  for (auto& name : files) {
    // Include .txt and .epub files (case-insensitive)
    if (name.length() >= 4) {
      String ext = name.substring(name.length() - 4);
      ext.toLowerCase();
      if (ext == String(".txt")) {
        sdFiles.push_back(name);
        continue;  // Avoid checking for .epub if we already matched .txt
      }
    }
    // Check for .epub extension (5 characters)
    if (name.length() >= 5) {
      String ext = name.substring(name.length() - 5);
      ext.toLowerCase();
      if (ext == String(".epub")) {
        sdFiles.push_back(name);
      }
    }
  }

  // Sort files alphabetically (case-sensitive using strcmp)
  std::sort(sdFiles.begin(), sdFiles.end(),
            [](const String& a, const String& b) { return std::strcmp(a.c_str(), b.c_str()) < 0; });

  // Restore saved selection if available and present in this folder
  sdSelectedIndex = 0;
  sdScrollOffset = 0;
  if (!sdFiles.empty()) {
    Settings& s = uiManager.getSettings();
    String saved = s.getString(String("filebrowser.selected"), String(""));
    if (saved.length() > 0) {
      for (size_t i = 0; i < sdFiles.size(); ++i) {
        if (sdFiles[i] == saved) {
          sdSelectedIndex = (int)i;
          if (sdSelectedIndex >= SD_LINES_PER_SCREEN)
            sdScrollOffset = sdSelectedIndex - SD_LINES_PER_SCREEN + 1;
          else
            sdScrollOffset = 0;
          break;
        }
      }
    }
  }
}
