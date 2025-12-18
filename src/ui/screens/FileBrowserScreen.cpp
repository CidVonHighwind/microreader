
#include "FileBrowserScreen.h"
#include <resources/fonts/FontManager.h>

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
  if (buttons.isPressed(Buttons::CONFIRM)) {
    confirm();
  } else if (buttons.isPressed(Buttons::LEFT)) {
    selectNext();
  } else if (buttons.isPressed(Buttons::RIGHT)) {
    selectPrev();
  }
}

void FileBrowserScreen::activate() {
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

  int totalHeight = drawable * lineHeight;
  int startY = (800 - totalHeight) / 2;  // center vertically (pageHeight = 800)

  for (int i = 0; i < drawable; ++i) {
    int idx = sdScrollOffset + i;
    String name = sdFiles[idx];
    // For display, strip the .txt extension if present but keep the stored
    // filename intact so confirm() can open it later.
    // For .epub files, keep the extension visible.
    String displayNameRaw = name;
    if (displayNameRaw.length() >= 4) {
      String ext = displayNameRaw.substring(displayNameRaw.length() - 4);
      ext.toLowerCase();
      if (ext == String(".txt")) {
        displayNameRaw = displayNameRaw.substring(0, displayNameRaw.length() - 4);
      }
    }

    if (displayNameRaw.length() > 30)
      displayNameRaw = displayNameRaw.substring(0, 27) + "...";

    String displayName;
    if (idx == sdSelectedIndex) {
      // Show both left and right markers around the selection and center the whole string
      displayName = String(">") + displayNameRaw + String("<");
    } else {
      displayName = displayNameRaw;
    }

    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(displayName.c_str(), 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (480 - (int)w) / 2;  // horizontal center (pageWidth = 480)
    int16_t rowY = startY + i * lineHeight;
    textRenderer.setCursor(centerX, rowY);
    textRenderer.print(displayName);
  }

  // Draw battery percentage at bottom-right of the screen
  {
    textRenderer.setFont(getMainFont());
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

  show();
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
