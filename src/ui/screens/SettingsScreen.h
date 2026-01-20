#ifndef SETTINGSSCREEN_H
#define SETTINGSSCREEN_H

#include "../../core/EInkDisplay.h"
#include "../../rendering/TextRenderer.h"
#include "Screen.h"

class Buttons;
class UIManager;

class SettingsScreen : public Screen {
 public:
  SettingsScreen(EInkDisplay& display, TextRenderer& renderer, UIManager& uiManager);

  void begin() override;
  void handleButtons(Buttons& buttons) override;
  void activate() override;
  void show() override;
  void shutdown() override {}

 private:
  EInkDisplay& display;
  TextRenderer& textRenderer;
  UIManager& uiManager;

  // Menu navigation
  int selectedIndex = 0;
  static constexpr int SETTINGS_COUNT = 8;

  // Setting values and their current indices
  int marginIndex = 1;
  int lineHeightIndex = 1;
  int alignmentIndex = 0;
  int showChapterNumbersIndex = 0;
  int fontFamilyIndex = 1;       // 0=NotoSans, 1=Bookerly
  int fontSizeIndex = 0;         // 0=Small(26), 1=Medium(28), 2=Large(30)
  int uiFontSizeIndex = 0;       // 0=Small(14), 1=Large(28)
  int flipPageButtonsIndex = 0;  // 0=Normal (LEFT=next), 1=Flipped (RIGHT=next)

  // Available values for each setting
  static constexpr int marginValues[] = {5, 10, 15, 20, 25, 30};
  static constexpr int marginValuesCount = 6;
  static constexpr int lineHeightValues[] = {0, 2, 4, 6, 8, 10};
  static constexpr int lineHeightValuesCount = 6;
  // Alignment: 0=LEFT, 1=CENTER, 2=RIGHT
  // showChapterNumbers: 0=OFF, 1=ON

  void renderSettings();
  void selectNext();
  void selectPrev();
  void toggleCurrentSetting();
  void loadSettings();
  void saveSettings();
  void applyFontSettings();
  void applyUIFontSettings();
  String getSettingName(int index);
  String getSettingValue(int index);
};

#endif
