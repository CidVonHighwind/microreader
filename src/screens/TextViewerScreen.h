#ifndef TEXT_VIEWER_SCREEN_H
#define TEXT_VIEWER_SCREEN_H

#include <vector>

#include "../TextRenderer.h"
#include "EInkDisplay.h"
#include "Screen.h"
#include "text view/TextLayout.h"

class TextViewerScreen : public Screen {
 public:
  TextViewerScreen(EInkDisplay& display, TextRenderer& renderer);
  ~TextViewerScreen();
  void loadTextFromProgmem();
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

  std::vector<String> pages;
  int totalPages = 0;
};

#endif
