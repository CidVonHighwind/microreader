
#include "FileBrowserScreen.h"

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

namespace {
constexpr int SCREEN_WIDTH = 480;
constexpr int SCREEN_HEIGHT = 800;
constexpr int TITLE_Y = 75;
constexpr int LINE_HEIGHT = 28;
constexpr int BATTERY_Y = 790;
constexpr int MAX_DISPLAY_NAME_LENGTH = 30;
constexpr int MAX_VISIBLE_FILES = 16;
}  // namespace

FileBrowserScreen::FileBrowserScreen(EInkDisplay& display, TextRenderer& renderer, SDCardManager& sdManager,
                                     UIManager& uiManager)
    : display(display), textRenderer(renderer), sdManager(sdManager), uiManager(uiManager) {}

void FileBrowserScreen::begin() {
  loadFolder();
}

void FileBrowserScreen::activate() {
  Settings& settings = uiManager.getSettings();
  int uiFontSize = 0;
  settings.getInt("settings.uiFontSize", uiFontSize);
  setTitleFont(&MenuHeader);
  setMainFont(uiFontSize == 0 ? &MenuFontSmall : &MenuFontBig);

  loadFolder();
}

void FileBrowserScreen::show() {
  render();
  display.displayBuffer(EInkDisplay::FAST_REFRESH);
}

void FileBrowserScreen::handleButtons(Buttons& buttons) {
  bool needsUpdate = false;
  bool shouldGoBack = false;
  bool shouldConfirm = false;

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

  if (shouldGoBack) {
    uiManager.showScreen(UIManager::ScreenId::Settings);
  } else if (shouldConfirm) {
    confirm();
  } else if (needsUpdate) {
    show();
  }
}

void FileBrowserScreen::confirm() {
  if (!files.empty()) {
    String fullPath = "/" + files[selectedIndex].filename;
    Serial.printf("Selected file: %s\n", fullPath.c_str());
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
  if (files.empty()) {
    return;
  }

  int fileCount = static_cast<int>(files.size());
  selectedIndex = (selectedIndex + offset % fileCount + fileCount) % fileCount;

  // Keep selection visible
  if (selectedIndex >= scrollOffset + MAX_VISIBLE_FILES) {
    scrollOffset = selectedIndex - MAX_VISIBLE_FILES + 1;
  } else if (selectedIndex < scrollOffset) {
    scrollOffset = selectedIndex;
  }

  // Persist selection
  Settings& settings = uiManager.getSettings();
  settings.setString("filebrowser.selected", files[selectedIndex].filename);
}

void FileBrowserScreen::render() {
  display.clearScreen(0xFF);
  textRenderer.setFrameBuffer(display.getFrameBuffer());
  textRenderer.setBitmapType(TextRenderer::BITMAP_BW);
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);

  // Title
  textRenderer.setFont(getTitleFont());
  int16_t x1, y1;
  uint16_t w, h;
  textRenderer.getTextBounds("Microreader", 0, 0, &x1, &y1, &w, &h);
  textRenderer.setCursor((SCREEN_WIDTH - w) / 2, TITLE_Y);
  textRenderer.print("Microreader");

  // File list
  textRenderer.setFont(getMainFont());
  int visibleCount = std::min(MAX_VISIBLE_FILES, static_cast<int>(files.size()) - scrollOffset);
  if (visibleCount <= 0) {
    return;
  }

  textRenderer.getTextBounds("Ag", 0, 0, &x1, &y1, &w, &h);
  int startY = (SCREEN_HEIGHT - visibleCount * LINE_HEIGHT) / 2 + h;

  for (int i = 0; i < visibleCount; ++i) {
    int fileIndex = scrollOffset + i;
    int rowY = startY + i * LINE_HEIGHT;
    const String& displayName = files[fileIndex].displayName;

    textRenderer.getTextBounds(displayName.c_str(), 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (SCREEN_WIDTH - w) / 2;

    if (fileIndex == selectedIndex) {
      // Selection bar
      display.fillRect(centerX - 4, rowY - h + 1, w + 8, h + 4, 0x00);
      textRenderer.setTextColor(TextRenderer::COLOR_WHITE);
    }

    textRenderer.setCursor(centerX, rowY);
    textRenderer.print(displayName);

    if (fileIndex == selectedIndex) {
      textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
    }
  }

  // Battery indicator
  textRenderer.setFont(&MenuFontSmall);
  String batteryText = String(g_battery.readPercentage()) + "%";
  textRenderer.getTextBounds(batteryText.c_str(), 0, 0, &x1, &y1, &w, &h);
  textRenderer.setCursor((SCREEN_WIDTH - w) / 2, BATTERY_Y);
  textRenderer.print(batteryText);
}

void FileBrowserScreen::loadFolder(int maxFiles) {
  files.clear();
  selectedIndex = 0;
  scrollOffset = 0;

  if (!sdManager.ready()) {
    Serial.println("SD not ready; cannot list files.");
    return;
  }

  // Collect supported files
  for (const auto& name : sdManager.listFiles("/", maxFiles)) {
    if (isSupportedFile(name)) {
      files.push_back(createFileEntry(name));
    }
  }

  // Sort alphabetically by display name
  std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
    return std::strcmp(a.displayName.c_str(), b.displayName.c_str()) < 0;
  });

  // Restore saved selection
  if (!files.empty()) {
    String saved = uiManager.getSettings().getString("filebrowser.selected", "");
    for (int i = 0; i < static_cast<int>(files.size()); ++i) {
      if (files[i].filename == saved) {
        selectedIndex = i;
        if (selectedIndex >= MAX_VISIBLE_FILES) {
          scrollOffset = selectedIndex - MAX_VISIBLE_FILES + 1;
        }
        break;
      }
    }
  }
}

FileEntry FileBrowserScreen::createFileEntry(const String& filename) const {
  String name = stripExtension(filename);

  // Truncate with ellipsis
  if (name.length() > MAX_DISPLAY_NAME_LENGTH) {
    name = name.substring(0, MAX_DISPLAY_NAME_LENGTH - 3) + "...";
  }

  return {filename, name};
}

String FileBrowserScreen::stripExtension(const String& filename) const {
  int dotIndex = filename.lastIndexOf('.');
  if (dotIndex > 0) {
    return filename.substring(0, dotIndex);
  }
  return filename;
}

bool FileBrowserScreen::isSupportedFile(const String& filename) const {
  return hasExtension(filename, ".txt") || hasExtension(filename, ".epub");
}

bool FileBrowserScreen::hasExtension(const String& filename, const char* ext) const {
  size_t extLen = strlen(ext);
  if (filename.length() < extLen) {
    return false;
  }
  String suffix = filename.substring(filename.length() - extLen);
  suffix.toLowerCase();
  return suffix == ext;
}
