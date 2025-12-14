#ifndef TEXT_VIEWER_SCREEN_H
#define TEXT_VIEWER_SCREEN_H

#include "../../content/providers/StringWordProvider.h"
#include "../../core/EInkDisplay.h"
#include "../../core/SDCardManager.h"
#include "../../rendering/TextRenderer.h"
#include "../../text/layout/LayoutStrategy.h"
#include "../UIManager.h"
#include "Screen.h"

class TextViewerScreen : public Screen {
 public:
  TextViewerScreen(EInkDisplay& display, TextRenderer& renderer, SDCardManager& sdManager, UIManager& uiManager);
  ~TextViewerScreen();

  void begin() override;
  void activate() override;

  // Load content from SD by path and display it
  void openFile(const String& sdPath);
  // Load text content (already in RAM) and split into pages.
  void loadTextFromString(const String& content);

  void nextPage();
  void prevPage();
  void jumpToNextChapter();
  void jumpToPreviousChapter();

  void showPage();

  // Generic show renders the current page
  void show() override;
  void handleButtons(class Buttons& buttons) override;
  // Called when device is powering down; save document position
  void shutdown() override;

  int currentChapter = 0;
  int pageStartIndex = 0;
  int pageEndIndex = 0;

 private:
  EInkDisplay& display;
  TextRenderer& textRenderer;
  LayoutStrategy* layoutStrategy;
  SDCardManager& sdManager;
  UIManager& uiManager;

  WordProvider* provider = nullptr;
  // Keep the loaded text alive for the lifetime of the provider
  String loadedText;
  LayoutStrategy::LayoutConfig layoutConfig;
  // Path of the currently opened SD file (empty when viewing from memory)
  String currentFilePath;
  // Path loaded from settings but not yet opened. begin() will set this and
  // show()/activate() will open it when the screen is shown so begin() remains
  // an init-only function and doesn't draw to the display.
  String pendingOpenPath;

  // Persist/load current reading position for `currentFilePath`
  void savePositionToFile();
  void loadPositionFromFile();
  // Persist/load viewer settings (last opened file path + layout config)
  void saveSettingsToFile();
  void loadSettingsFromFile();
};

#endif
