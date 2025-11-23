#ifndef TEXT_VIEWER_SCREEN_H
#define TEXT_VIEWER_SCREEN_H

#include <vector>

#include "../SDCardManager.h"
#include "../TextRenderer.h"
#include "EInkDisplay.h"
#include "Screen.h"
#include "text view/TextLayout.h"

class TextViewerScreen : public Screen {
 public:
  TextViewerScreen(EInkDisplay& display, TextRenderer& renderer, SDCardManager& sdManager);
  ~TextViewerScreen();
  // Load content from SD by path and display it
  void openFile(const String& sdPath);
  void loadTextFromProgmem();
  // Load text content (already in RAM) and split into pages.
  void loadTextFromString(const String& content);
  void begin() override;
  void activate(int context = 0) override;
  void showPage(int page);
  int nextPage();
  int prevPage();
  int getTotalPages() const;

  // Generic show renders the current page
  void show() override;

  void handleButtons(class Buttons& buttons) override;

  int currentPage = 0;

 private:
  EInkDisplay& display;
  TextRenderer& textRenderer;
  TextLayout* textLayout;
  SDCardManager& sdManager;

  std::vector<String> pages;
  int totalPages = 0;
};

#endif
