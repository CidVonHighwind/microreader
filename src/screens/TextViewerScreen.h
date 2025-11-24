#ifndef TEXT_VIEWER_SCREEN_H
#define TEXT_VIEWER_SCREEN_H

#include "../SDCardManager.h"
#include "../TextRenderer.h"
#include "../UIManager.h"
#include "EInkDisplay.h"
#include "Screen.h"
#include "text view/StringWordProvider.h"

class TextViewerScreen : public Screen {
 public:
  TextViewerScreen(EInkDisplay& display, TextRenderer& renderer, SDCardManager& sdManager, UIManager& uiManager);
  ~TextViewerScreen();
  // Load content from SD by path and display it
  void openFile(const String& sdPath);
  void loadTextFromProgmem();
  // Load text content (already in RAM) and split into pages.
  void loadTextFromString(const String& content);
  void begin() override;
  void activate(int context = 0) override;
  void showPage();
  void nextPage();
  void prevPage();
  int getTotalPages() const;

  // Generic show renders the current page
  void show() override;

  void handleButtons(class Buttons& buttons) override;

  int pageStartIndex = 0;
  int currentIndex = 0;

 private:
  EInkDisplay& display;
  TextRenderer& textRenderer;
  TextLayout* textLayout;
  SDCardManager& sdManager;
  UIManager& uiManager;

  StringWordProvider* provider = nullptr;
  TextLayout::LayoutConfig layoutConfig;
};

#endif
