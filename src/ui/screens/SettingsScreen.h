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
  // Setting indices enum
  enum SettingIndex {
    SETTING_MARGINS = 0,
    SETTING_LINE_SPACING = 1,
    SETTING_ALIGNMENT = 2,
    SETTING_CHAPTER_NUMBERS = 3,
    SETTING_PAGE_BUTTONS = 4,
    SETTING_FONT_FAMILY = 5,
    SETTING_FONT_SIZE = 6,
    SETTING_UI_FONT_SIZE = 7
  };

  // Display and layout constants
  static constexpr int DISPLAY_WIDTH = 480;
  static constexpr int DISPLAY_HEIGHT = 800;
  static constexpr int TITLE_Y = 75;
  static constexpr int BATTERY_Y = 790;
  static constexpr int MENU_LINE_HEIGHT = 28;

  // Setting limits
  static constexpr int ALIGNMENT_COUNT = 3;
  static constexpr int FONT_FAMILY_COUNT = 2;
  static constexpr int FONT_SIZE_COUNT = 3;
  static constexpr int TOGGLE_COUNT = 2;

  // Default values
  static constexpr int DEFAULT_MARGIN = 10;
  static constexpr int DEFAULT_LINE_HEIGHT = 30;

  enum MenuItemType { ITEM_SETTING, ITEM_SPACER };

  struct MenuItem {
    MenuItemType type;
    int settingIndex;  // index for ITEM_SETTING, unused for ITEM_SPACER
  };

  EInkDisplay& display;
  TextRenderer& textRenderer;
  UIManager& uiManager;

  // Menu structure with settings and spacers
  static constexpr MenuItem menuItems[] = {
      {ITEM_SETTING, SETTING_FONT_SIZE      },
      {ITEM_SETTING, SETTING_FONT_FAMILY    },
      {ITEM_SPACER,  0                      },
      {ITEM_SETTING, SETTING_MARGINS        },
      {ITEM_SETTING, SETTING_LINE_SPACING   },
      {ITEM_SETTING, SETTING_ALIGNMENT      },
      {ITEM_SPACER,  0                      },
      {ITEM_SETTING, SETTING_CHAPTER_NUMBERS},
      {ITEM_SETTING, SETTING_PAGE_BUTTONS   },
      {ITEM_SPACER,  0                      },
      {ITEM_SETTING, SETTING_UI_FONT_SIZE   },
  };
  static constexpr int MENU_ITEM_COUNT = 11;
  static constexpr int SETTINGS_COUNT = 8;

  // Menu navigation
  int selectedIndex = 0;

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
  String getSettingName(int settingIndex);
  String getSettingValue(int settingIndex);
  bool isSpacer(int menuIndex) const;
  int getSettingIndexFromMenu(int menuIndex) const;
};

#endif
