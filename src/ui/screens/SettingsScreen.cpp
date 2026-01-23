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
constexpr SettingsScreen::MenuItem SettingsScreen::menuItems[];
constexpr int SettingsScreen::MENU_ITEM_COUNT;
constexpr int SettingsScreen::SETTINGS_COUNT;

SettingsScreen::SettingsScreen(EInkDisplay& display, TextRenderer& renderer, UIManager& uiManager)
    : display(display), textRenderer(renderer), uiManager(uiManager) {}

void SettingsScreen::begin() {
  loadSettings();
}

void SettingsScreen::handleButtons(Buttons& buttons) {
  bool needsUpdate = false;
  bool shouldGoBack = false;

  // Consume all queued button presses
  uint8_t btn;
  while ((btn = buttons.consumeNextPress()) != Buttons::NONE) {
    switch (btn) {
      case Buttons::BACK:
        shouldGoBack = true;
        break;

      case Buttons::LEFT:
        selectNext();
        needsUpdate = true;
        break;

      case Buttons::RIGHT:
        selectPrev();
        needsUpdate = true;
        break;

      case Buttons::CONFIRM:
        if (!isSpacer(selectedIndex)) {
          toggleCurrentSetting();
          needsUpdate = true;
        }
        break;
    }
  }

  // Handle navigation after processing all inputs
  if (shouldGoBack) {
    saveSettings();
    uiManager.showScreen(uiManager.getPreviousScreen());
    return;
  }

  // Only update display once after processing all inputs
  if (needsUpdate) {
    saveSettings();
    show();
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
    int16_t centerX = (DISPLAY_WIDTH - (int)w) / 2;
    textRenderer.setCursor(centerX, TITLE_Y);
    textRenderer.print(title);
  }

  textRenderer.setFont(getMainFont());

  // Render menu items
  const int spacerHeight = MENU_LINE_HEIGHT / 2;                            // Spacers are 50% smaller
  int totalHeight = MENU_ITEM_COUNT * MENU_LINE_HEIGHT - spacerHeight * 3;  // Subtract spacer reductions
  int startY = (DISPLAY_HEIGHT - totalHeight) / 2;                          // center vertically

  int currentY = startY;
  for (int i = 0; i < MENU_ITEM_COUNT; ++i) {
    // Skip rendering spacer lines
    if (isSpacer(i)) {
      currentY += spacerHeight;
      continue;
    }

    int settingIndex = getSettingIndexFromMenu(i);
    String displayName = getSettingName(settingIndex);
    displayName += ": ";
    displayName += getSettingValue(settingIndex);

    if (i == selectedIndex) {
      displayName = String(">") + displayName + String("<");
    }

    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(displayName.c_str(), 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (DISPLAY_WIDTH - (int)w) / 2;
    textRenderer.setCursor(centerX, currentY);
    textRenderer.print(displayName);
    currentY += MENU_LINE_HEIGHT;
  }

  // Draw battery percentage at bottom
  {
    textRenderer.setFont(&MenuFontSmall);  // Always use small font for battery
    int pct = g_battery.readPercentage();
    String pctStr = String(pct) + "%";
    int16_t bx1, by1;
    uint16_t bw, bh;
    textRenderer.getTextBounds(pctStr.c_str(), 0, 0, &bx1, &by1, &bw, &bh);
    int16_t bx = (DISPLAY_WIDTH - (int)bw) / 2;
    textRenderer.setCursor(bx, BATTERY_Y);
    textRenderer.print(pctStr);
  }
}

void SettingsScreen::selectNext() {
  selectedIndex++;
  if (selectedIndex >= MENU_ITEM_COUNT)
    selectedIndex = 0;
  // Skip spacers
  while (isSpacer(selectedIndex)) {
    selectedIndex++;
    if (selectedIndex >= MENU_ITEM_COUNT)
      selectedIndex = 0;
  }
}

void SettingsScreen::selectPrev() {
  selectedIndex--;
  if (selectedIndex < 0)
    selectedIndex = MENU_ITEM_COUNT - 1;
  // Skip spacers
  while (isSpacer(selectedIndex)) {
    selectedIndex--;
    if (selectedIndex < 0)
      selectedIndex = MENU_ITEM_COUNT - 1;
  }
}

void SettingsScreen::toggleCurrentSetting() {
  if (isSpacer(selectedIndex))
    return;

  int settingIndex = getSettingIndexFromMenu(selectedIndex);

  switch (settingIndex) {
    case SETTING_MARGINS:
      marginIndex++;
      if (marginIndex >= marginValuesCount)
        marginIndex = 0;
      break;
    case SETTING_LINE_SPACING:
      lineHeightIndex++;
      if (lineHeightIndex >= lineHeightValuesCount)
        lineHeightIndex = 0;
      break;
    case SETTING_ALIGNMENT:
      alignmentIndex++;
      if (alignmentIndex >= ALIGNMENT_COUNT)
        alignmentIndex = 0;
      break;
    case SETTING_CHAPTER_NUMBERS:
      showChapterNumbersIndex = 1 - showChapterNumbersIndex;
      break;
    case SETTING_PAGE_BUTTONS:
      flipPageButtonsIndex = 1 - flipPageButtonsIndex;
      break;
    case SETTING_FONT_FAMILY:
      fontFamilyIndex++;
      if (fontFamilyIndex >= FONT_FAMILY_COUNT)
        fontFamilyIndex = 0;
      applyFontSettings();
      break;
    case SETTING_FONT_SIZE:
      fontSizeIndex++;
      if (fontSizeIndex >= FONT_SIZE_COUNT)
        fontSizeIndex = 0;
      applyFontSettings();
      break;
    case SETTING_UI_FONT_SIZE:
      uiFontSizeIndex = 1 - uiFontSizeIndex;
      applyUIFontSettings();
      break;
  }
}

void SettingsScreen::loadSettings() {
  Settings& s = uiManager.getSettings();

  // Load horizontal margins (applies to both left and right)
  int margin = DEFAULT_MARGIN;
  if (s.getInt(String("settings.margin"), margin)) {
    for (int i = 0; i < marginValuesCount; i++) {
      if (marginValues[i] == margin) {
        marginIndex = i;
        break;
      }
    }
  }

  // Load line height
  int lineHeight = DEFAULT_LINE_HEIGHT;
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
    case SETTING_MARGINS:
      return "Margins";
    case SETTING_LINE_SPACING:
      return "Line Spacing";
    case SETTING_ALIGNMENT:
      return "Alignment";
    case SETTING_CHAPTER_NUMBERS:
      return "Chapter Numbers";
    case SETTING_PAGE_BUTTONS:
      return "Page Buttons";
    case SETTING_FONT_FAMILY:
      return "Font Family";
    case SETTING_FONT_SIZE:
      return "Font Size";
    case SETTING_UI_FONT_SIZE:
      return "UI Font Size";
    default:
      return "";
  }
}

String SettingsScreen::getSettingValue(int index) {
  switch (index) {
    case SETTING_MARGINS:
      return String(marginValues[marginIndex]);
    case SETTING_LINE_SPACING:
      return String(lineHeightValues[lineHeightIndex]);
    case SETTING_ALIGNMENT:
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
    case SETTING_CHAPTER_NUMBERS:
      return showChapterNumbersIndex ? "On" : "Off";
    case SETTING_PAGE_BUTTONS:
      return flipPageButtonsIndex ? "Inverted" : "Normal";
    case SETTING_FONT_FAMILY:
      switch (fontFamilyIndex) {
        case 0:
          return "NotoSans";
        case 1:
          return "Bookerly";
        default:
          return "Unknown";
      }
    case SETTING_FONT_SIZE:
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
    case SETTING_UI_FONT_SIZE:
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

bool SettingsScreen::isSpacer(int menuIndex) const {
  if (menuIndex < 0 || menuIndex >= MENU_ITEM_COUNT)
    return false;
  return menuItems[menuIndex].type == ITEM_SPACER;
}

int SettingsScreen::getSettingIndexFromMenu(int menuIndex) const {
  if (menuIndex < 0 || menuIndex >= MENU_ITEM_COUNT)
    return 0;
  return menuItems[menuIndex].settingIndex;
}
