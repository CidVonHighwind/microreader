#include "screens/TextViewerScreen.h"

#include <Arduino.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>

#include "Buttons.h"
#include "sample_text.h"
#include "text view/KnuthPlassLayoutStrategy.h"

TextViewerScreen::TextViewerScreen(EInkDisplay& display, TextRenderer& renderer)
    : display(display), textRenderer(renderer), textLayout(new TextLayout()) {}

TextViewerScreen::~TextViewerScreen() {
  delete textLayout;
}

void TextViewerScreen::begin() {
  loadTextFromProgmem();
}

void TextViewerScreen::activate(int context) {
  // context is page index
  int page = context;
  if (page < 0)
    page = 0;
  if (page >= totalPages)
    page = max(0, totalPages - 1);
  showPage(page);
}

// Ensure member function is in class scope
void TextViewerScreen::handleButtons(Buttons& buttons) {
  if (buttons.wasPressed(Buttons::LEFT)) {
    nextPage();
  } else if (buttons.wasPressed(Buttons::RIGHT)) {
    prevPage();
  }
}

void TextViewerScreen::show() {
  showPage(currentPage);
}

void TextViewerScreen::showPage(int page) {
  currentPage = page;

  textRenderer.clearText();
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFont(&FreeSans12pt7b);

  if (page >= 0 && page < totalPages) {
    String pageText = pages[page];

    TextLayout::LayoutConfig config;
    config.marginLeft = 10;
    config.marginRight = 10;
    config.marginTop = 40;
    config.marginBottom = 40;
    config.lineHeight = 30;
    config.minSpaceWidth = 10;
    config.pageWidth = 480;
    config.pageHeight = 800;
    config.alignment = LayoutStrategy::ALIGN_LEFT;

    textLayout->layoutText(pageText, textRenderer, config);
  }

  // page indicator
  textRenderer.setFont();
  String pageIndicator = String(page + 1) + "/" + String(totalPages);
  int16_t x1, y1;
  uint16_t w, h;
  textRenderer.getTextBounds(pageIndicator.c_str(), 0, 0, &x1, &y1, &w, &h);
  int16_t centerX = (480 - w) / 2;
  textRenderer.setCursor(centerX, 780);
  textRenderer.print(pageIndicator);

  display.displayBuffer(EInkDisplay::FAST_REFRESH);
}

int TextViewerScreen::nextPage() {
  currentPage = (currentPage + 1) % max(1, totalPages);
  showPage(currentPage);
  return currentPage;
}

int TextViewerScreen::prevPage() {
  if (totalPages <= 0)
    return currentPage;
  currentPage = (currentPage - 1 + totalPages) % totalPages;
  showPage(currentPage);
  return currentPage;
}

void TextViewerScreen::loadTextFromProgmem() {
  String fullText = "";
  char c;
  int i = 0;
  while ((c = pgm_read_byte(&SAMPLE_TEXT[i])) != '\0') {
    fullText += c;
    i++;
  }

  String currentPageText = "";
  int startIndex = 0;

  while (startIndex < fullText.length()) {
    int pageDelimiter = fullText.indexOf("---PAGE---", startIndex);
    if (pageDelimiter == -1) {
      currentPageText = fullText.substring(startIndex);
      currentPageText.trim();
      if (currentPageText.length() > 0) {
        pages.push_back(currentPageText);
      }
      break;
    } else {
      currentPageText = fullText.substring(startIndex, pageDelimiter);
      currentPageText.trim();
      if (currentPageText.length() > 0) {
        pages.push_back(currentPageText);
      }
      startIndex = pageDelimiter + 10;
    }
  }

  totalPages = pages.size();
}
