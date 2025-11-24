#include "screens/TextViewerScreen.h"

#include <Arduino.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>

#include "../SDCardManager.h"
#include "Buttons.h"
#include "sample_text.h"
#include "text view/KnuthPlassLayoutStrategy.h"
#include "text view/StringWordProvider.h"

TextViewerScreen::TextViewerScreen(EInkDisplay& display, TextRenderer& renderer, SDCardManager& sdManager,
                                   UIManager& uiManager)
    : display(display),
      textRenderer(renderer),
      textLayout(new TextLayout()),
      sdManager(sdManager),
      uiManager(uiManager) {
  // Initialize layout config
  layoutConfig.marginLeft = 10;
  layoutConfig.marginRight = 10;
  layoutConfig.marginTop = 40;
  layoutConfig.marginBottom = 40;
  layoutConfig.lineHeight = 30;
  layoutConfig.minSpaceWidth = 10;
  layoutConfig.pageWidth = 480;
  layoutConfig.pageHeight = 800;
  layoutConfig.alignment = LayoutStrategy::ALIGN_LEFT;
}

TextViewerScreen::~TextViewerScreen() {
  delete textLayout;
  delete provider;
}

void TextViewerScreen::begin() {
  loadTextFromProgmem();
}

void TextViewerScreen::activate(int context) {
  // Ignore context for now, just start from beginning
  currentIndex = 0;
  show();
}

// Ensure member function is in class scope
void TextViewerScreen::handleButtons(Buttons& buttons) {
  if (buttons.wasPressed(Buttons::BACK)) {
    uiManager.showScreen(UIManager::ScreenId::FileBrowser);
  } else if (buttons.wasPressed(Buttons::LEFT)) {
    nextPage();
  } else if (buttons.wasPressed(Buttons::RIGHT)) {
    prevPage();
  }
}

void TextViewerScreen::show() {
  showPage();
}

void TextViewerScreen::showPage() {
  Serial.println("showPage start");

  if (!provider)
    return;

  float displayPercentage = provider->getPercentage();
  provider->setPosition(currentIndex);

  textRenderer.clearText();
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFont(&FreeSans12pt7b);

  pageStartIndex = provider->getCurrentIndex();

  // print out current percentage
  float currentPercentage = provider->getPercentage();
  Serial.printf("Layout at %.1f%%\n", currentPercentage * 100.0f);

  Serial.println("Before layout");
  try {
    textLayout->layoutText(*provider, textRenderer, layoutConfig);
  } catch (const std::exception& e) {
    Serial.printf("Exception during layout: %s\n", e.what());
    abort();
  } catch (...) {
    Serial.println("Unknown exception during layout");
    abort();
  }
  Serial.println("After layout");

  // Update currentIndex to where the layout ended
  currentIndex = provider->getCurrentIndex();

  // page indicator - now shows percentage
  {
    Serial.println("Before page indicator");
    textRenderer.setFont();
    if (!provider->hasNextWord()) {
      displayPercentage = 1.0f;  // Show 100% if we've reached the end
    }
    String percentageIndicator = String((int)(displayPercentage * 100)) + "%";
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(percentageIndicator.c_str(), 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (480 - w) / 2;
    textRenderer.setCursor(centerX, 780);
    textRenderer.print(percentageIndicator);
  }

  Serial.println("Before display");
  display.displayBuffer(EInkDisplay::FAST_REFRESH);
  Serial.println("After display");
}

void TextViewerScreen::nextPage() {
  // Check if there are more words before advancing
  if (provider && provider->hasNextWord()) {
    currentIndex = provider->getCurrentIndex();
    showPage();
  }
}

void TextViewerScreen::prevPage() {
  if (!provider || currentIndex <= 0)
    return;

  // Use the layout strategy to find the exact start of the previous page
  // Find where the previous page starts
  int previousPageStart =
      textLayout->getStrategy()->getPreviousPageStart(*provider, textRenderer, layoutConfig, pageStartIndex);

  // Set currentIndex to the start of the previous page
  currentIndex = previousPageStart;

  // Do normal forward layout from this position
  showPage();
}

void TextViewerScreen::loadTextFromProgmem() {
  String fullText = "";
  char c;
  int i = 0;
  while ((c = pgm_read_byte(&SAMPLE_TEXT[i])) != '\0') {
    fullText += c;
    i++;
  }

  // Create provider for the entire content
  delete provider;
  provider = new StringWordProvider(fullText);
  currentIndex = 0;
  showPage();
}

void TextViewerScreen::loadTextFromString(const String& content) {
  // Create provider for the entire content
  delete provider;
  String fullText = content;
  if (fullText.length() > 0) {
    provider = new StringWordProvider(fullText);
  } else {
    provider = nullptr;
  }

  currentIndex = 0;
}

void TextViewerScreen::openFile(const String& sdPath) {
  if (!sdManager.ready()) {
    Serial.println("TextViewerScreen: SD not ready; cannot open file.");
    return;
  }

  Serial.println("Before readFile");
  String content = sdManager.readFile(sdPath.c_str());
  Serial.printf("After readFile, length: %d\n", content.length());
  if (content.length() == 0) {
    Serial.printf("TextViewerScreen: failed to read %s\n", sdPath.c_str());
    return;
  }

  Serial.println("Before loadTextFromString");
  loadTextFromString(content);
  Serial.println("After loadTextFromString");
}
