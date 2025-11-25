#ifndef TEXT_VIEWER_SCREEN_H
#define TEXT_VIEWER_SCREEN_H

#include "../SDCardManager.h"
#include "../UIManager.h"
#include "../text_renderer/TextRenderer.h"
#include "EInkDisplay.h"
#include "Screen.h"
#include "text view/LayoutStrategy.h"
#include "text view/StringWordProvider.h"

class TextViewerScreen : public Screen {
 public:
  TextViewerScreen(EInkDisplay& display, TextRenderer& renderer, SDCardManager& sdManager, UIManager& uiManager);
  ~TextViewerScreen();

  void begin() override;
  void activate(int context = 0) override;

  // Load content from SD by path and display it
  void openFile(const String& sdPath);
  // Load text content (already in RAM) and split into pages.
  void loadTextFromString(const String& content);

  void nextPage();
  void prevPage();

  void showPage();

  // Generic show renders the current page
  void show() override;
  void handleButtons(class Buttons& buttons) override;

  int pageStartIndex = 0;
  int pageEndIndex = 0;

 private:
  EInkDisplay& display;
  TextRenderer& textRenderer;
  LayoutStrategy* layoutStrategy;
  SDCardManager& sdManager;
  UIManager& uiManager;

  StringWordProvider* provider = nullptr;
  LayoutStrategy::LayoutConfig layoutConfig;
};

#endif
