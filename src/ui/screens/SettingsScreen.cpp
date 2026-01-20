#include "SettingsScreen.h"

#include <resources/fonts/FontDefinitions.h>
#include <resources/fonts/FontManager.h>
#include <resources/fonts/other/MenuFontBig.h>
#include <resources/fonts/other/MenuFontSmall.h>
#include <resources/fonts/other/MenuHeader.h>

#include "../../core/BatteryMonitor.h"
#include "../../core/Buttons.h"
#include "../../core/Settings.h"
#include "../UIManager.h"

constexpr int SettingsScreen::marginValues[];
constexpr int SettingsScreen::lineHeightValues[];

SettingsScreen::SettingsScreen(EInkDisplay& display, TextRenderer& renderer, UIManager& uiManager)
    : display(display), textRenderer(renderer), uiManager(uiManager) {}

void SettingsScreen::begin() {
  loadSettings();
}

void SettingsScreen::handleButtons(Buttons& buttons) {
  if (buttons.isPressed(Buttons::BACK)) {
    saveSettings();
    // Return to the screen we came from
    uiManager.showScreen(uiManager.getPreviousScreen());
  } else if (buttons.isPressed(Buttons::LEFT)) {
    selectNext();
  } else if (buttons.isPressed(Buttons::RIGHT)) {
    selectPrev();
  } else if (buttons.isPressed(Buttons::CONFIRM)) {
    toggleCurrentSetting();
  }
}

void SettingsScreen::activate() {
  loadSettings();
}

void SettingsScreen::show() {
  renderSettings();
  display.displayBuffer(EInkDisplay::FAST_REFRESH);
}

void SettingsScreen::renderSettings() {
  display.clearScreen(0xFF);
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFont(getTitleFont());

  // Set framebuffer to BW buffer for rendering
  textRenderer.setFrameBuffer(display.getFrameBuffer());
  textRenderer.setBitmapType(TextRenderer::BITMAP_BW);

  // Center the title horizontally
  {
    const char* title = "Settings";
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (480 - (int)w) / 2;
    textRenderer.setCursor(centerX, 75);
    textRenderer.print(title);
  }

  textRenderer.setFont(getMainFont());

  // Render settings list
  const int lineHeight = 28;
  int totalHeight = SETTINGS_COUNT * lineHeight;
  int startY = (800 - totalHeight) / 2;  // center vertically

  for (int i = 0; i < SETTINGS_COUNT; ++i) {
    String displayName = getSettingName(i);
    displayName += ": ";
    displayName += getSettingValue(i);

    if (i == selectedIndex) {
      displayName = String(">") + displayName + String("<");
    }

    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(displayName.c_str(), 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (480 - (int)w) / 2;
    int16_t rowY = startY + i * lineHeight;
    textRenderer.setCursor(centerX, rowY);
    textRenderer.print(displayName);
  }

  // Draw battery percentage at bottom
  {
    textRenderer.setFont(&MenuFontSmall);  // Always use small font for battery
    int pct = g_battery.readPercentage();
    String pctStr = String(pct) + "%";
    int16_t bx1, by1;
    uint16_t bw, bh;
    textRenderer.getTextBounds(pctStr.c_str(), 0, 0, &bx1, &by1, &bw, &bh);
    int16_t bx = (480 - (int)bw) / 2;
    int16_t by = 790;
    textRenderer.setCursor(bx, by);
    textRenderer.print(pctStr);
  }
}

void SettingsScreen::selectNext() {
  selectedIndex++;
  if (selectedIndex >= SETTINGS_COUNT)
    selectedIndex = 0;
  show();
}

void SettingsScreen::selectPrev() {
  selectedIndex--;
  if (selectedIndex < 0)
    selectedIndex = SETTINGS_COUNT - 1;
  show();
}

void SettingsScreen::toggleCurrentSetting() {
  switch (selectedIndex) {
    case 0:  // Horizontal Margins
      marginIndex++;
      if (marginIndex >= marginValuesCount)
        marginIndex = 0;
      break;
    case 1:  // Line Height
      lineHeightIndex++;
      if (lineHeightIndex >= lineHeightValuesCount)
        lineHeightIndex = 0;
      break;
    case 2:  // Alignment
      alignmentIndex++;
      if (alignmentIndex >= 3)
        alignmentIndex = 0;
      break;
    case 3:  // Show Chapter Numbers
      showChapterNumbersIndex = 1 - showChapterNumbersIndex;
      break;
    case 4:  // Flip Page Buttons
      flipPageButtonsIndex = 1 - flipPageButtonsIndex;
      break;
    case 5:  // Font Family
      fontFamilyIndex++;
      if (fontFamilyIndex >= 2)
        fontFamilyIndex = 0;
      applyFontSettings();
      break;
    case 6:  // Font Size
      fontSizeIndex++;
      if (fontSizeIndex >= 3)
        fontSizeIndex = 0;
      applyFontSettings();
      break;
    case 7:  // UI Font Size
      uiFontSizeIndex = 1 - uiFontSizeIndex;
      applyUIFontSettings();
      break;
  }
  saveSettings();
  show();
}

void SettingsScreen::loadSettings() {
  Settings& s = uiManager.getSettings();

  // Load horizontal margins (applies to both left and right)
  int margin = 10;
  if (s.getInt(String("settings.margin"), margin)) {
    for (int i = 0; i < marginValuesCount; i++) {
      if (marginValues[i] == margin) {
        marginIndex = i;
        break;
      }
    }
  }

  // Load line height
  int lineHeight = 30;
  if (s.getInt(String("settings.lineHeight"), lineHeight)) {
    for (int i = 0; i < lineHeightValuesCount; i++) {
      if (lineHeightValues[i] == lineHeight) {
        lineHeightIndex = i;
        break;
      }
    }
  }

  // Load alignment
  int alignment = 0;
  if (s.getInt(String("settings.alignment"), alignment)) {
    alignmentIndex = alignment;
  }

  // Load show chapter numbers
  int showChapters = 1;
  if (s.getInt(String("settings.showChapterNumbers"), showChapters)) {
    showChapterNumbersIndex = showChapters;
  }

  // Load font family (0=NotoSans, 1=Bookerly)
  int fontFamily = 1;
  if (s.getInt(String("settings.fontFamily"), fontFamily)) {
    fontFamilyIndex = fontFamily;
  }

  // Load font size (0=Small, 1=Medium, 2=Large)
  int fontSize = 0;
  if (s.getInt(String("settings.fontSize"), fontSize)) {
    fontSizeIndex = fontSize;
  }

  // Load UI font size (0=Small/14, 1=Large/28)
  int uiFontSize = 0;
  if (s.getInt(String("settings.uiFontSize"), uiFontSize)) {
    uiFontSizeIndex = uiFontSize;
  }

  // Load flip page buttons (0=Normal, 1=Flipped)
  int flipPageButtons = 0;
  if (s.getInt(String("settings.flipPageButtons"), flipPageButtons)) {
    flipPageButtonsIndex = flipPageButtons;
  }

  // Apply the loaded font settings
  applyFontSettings();
  applyUIFontSettings();
}

void SettingsScreen::saveSettings() {
  Settings& s = uiManager.getSettings();

  s.setInt(String("settings.margin"), marginValues[marginIndex]);
  s.setInt(String("settings.lineHeight"), lineHeightValues[lineHeightIndex]);
  s.setInt(String("settings.alignment"), alignmentIndex);
  s.setInt(String("settings.showChapterNumbers"), showChapterNumbersIndex);
  s.setInt(String("settings.fontFamily"), fontFamilyIndex);
  s.setInt(String("settings.fontSize"), fontSizeIndex);
  s.setInt(String("settings.uiFontSize"), uiFontSizeIndex);
  s.setInt(String("settings.flipPageButtons"), flipPageButtonsIndex);

  if (!s.save()) {
    Serial.println("SettingsScreen: Failed to write settings.cfg");
  }
}

String SettingsScreen::getSettingName(int index) {
  switch (index) {
    case 0:
      return "Margins";
    case 1:
      return "Line Spacing";
    case 2:
      return "Alignment";
    case 3:
      return "Chapter Numbers";
    case 4:
      return "Flip Page Buttons";
    case 5:
      return "Font Family";
    case 6:
      return "Font Size";
    case 7:
      return "UI Font Size";
    default:
      return "";
  }
}

String SettingsScreen::getSettingValue(int index) {
  switch (index) {
    case 0:
      return String(marginValues[marginIndex]);
    case 1:
      return String(lineHeightValues[lineHeightIndex]);
    case 2:
      switch (alignmentIndex) {
        case 0:
          return "Left";
        case 1:
          return "Center";
        case 2:
          return "Right";
        default:
          return "Unknown";
      }
    case 3:
      return showChapterNumbersIndex ? "On" : "Off";
    case 4:
      return flipPageButtonsIndex ? "Right=Next" : "Left=Next";
    case 5:
      switch (fontFamilyIndex) {
        case 0:
          return "NotoSans";
        case 1:
          return "Bookerly";
        default:
          return "Unknown";
      }
    case 6:
      switch (fontSizeIndex) {
        case 0:
          return "Small";
        case 1:
          return "Medium";
        case 2:
          return "Large";
        default:
          return "Unknown";
      }
    case 7:
      return uiFontSizeIndex ? "Large" : "Small";
    default:
      return "";
  }
}

void SettingsScreen::applyFontSettings() {
  // Determine which font family to use based on settings
  FontFamily* targetFamily = nullptr;

  if (fontFamilyIndex == 0) {  // NotoSans
    switch (fontSizeIndex) {
      case 0:
        targetFamily = &notoSans26Family;
        break;
      case 1:
        targetFamily = &notoSans28Family;
        break;
      case 2:
        targetFamily = &notoSans30Family;
        break;
    }
  } else if (fontFamilyIndex == 1) {  // Bookerly
    switch (fontSizeIndex) {
      case 0:
        targetFamily = &bookerly26Family;
        break;
      case 1:
        targetFamily = &bookerly28Family;
        break;
      case 2:
        targetFamily = &bookerly30Family;
        break;
    }
  }

  if (targetFamily) {
    setCurrentFontFamily(targetFamily);
  }
}

void SettingsScreen::applyUIFontSettings() {
  // Set main and title fonts for UI elements
  // Always use MenuHeader for headers/titles
  setTitleFont(&MenuHeader);

  if (uiFontSizeIndex == 0) {
    setMainFont(&MenuFontSmall);
  } else {
    setMainFont(&MenuFontBig);
  }
}
