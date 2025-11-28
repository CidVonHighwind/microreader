#include "screens/TextViewerScreen.h"

#include <Arduino.h>
#include <Fonts/Font16.h>
#include <Fonts/NotoSans26.h>

#include "../SDCardManager.h"
#include "Buttons.h"
#include "text view/GreedyLayoutStrategy.h"
#include "text view/KnuthPlassLayoutStrategy.h"
#include "text view/StringWordProvider.h"

TextViewerScreen::TextViewerScreen(EInkDisplay& display, TextRenderer& renderer, SDCardManager& sdManager,
                                   UIManager& uiManager)
    : display(display),
      textRenderer(renderer),
      layoutStrategy(new GreedyLayoutStrategy()),
      sdManager(sdManager),
      uiManager(uiManager) {
  // Initialize layout config
  layoutConfig.marginLeft = 10;
  layoutConfig.marginRight = 10;
  layoutConfig.marginTop = 15;
  layoutConfig.marginBottom = 40;
  layoutConfig.lineHeight = 30;
  layoutConfig.minSpaceWidth = 8;
  layoutConfig.pageWidth = 480;
  layoutConfig.pageHeight = 800;
  layoutConfig.alignment = LayoutStrategy::ALIGN_LEFT;
}

TextViewerScreen::~TextViewerScreen() {
  delete layoutStrategy;
  delete provider;
}

void TextViewerScreen::begin() {}

void TextViewerScreen::activate(int context) {
  pageStartIndex = 0;
  showPage();
}

// Ensure member function is in class scope
void TextViewerScreen::handleButtons(Buttons& buttons) {
  if (buttons.wasPressed(Buttons::BACK)) {
    uiManager.showScreen(UIManager::ScreenId::FileBrowser);
  } else if (buttons.wasPressed(Buttons::LEFT)) {
    nextPage();
  } else if (buttons.wasPressed(Buttons::RIGHT)) {
    prevPage();
  } else if (buttons.wasPressed(Buttons::VOLUME_UP)) {
    // switch through alignments (cycle through enum values safely)
    layoutConfig.alignment =
        static_cast<LayoutStrategy::TextAlignment>((static_cast<int>(layoutConfig.alignment) + 1) % 3);
    showPage();
  }
}

void TextViewerScreen::show() {
  showPage();
}

void TextViewerScreen::showPage() {
  Serial.println("showPage start");

  if (!provider)
    return;

  display.clearScreen(0xFF);
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFont(&NotoSans26);

  try {
    // print out current percentage
    Serial.print("Page start: ");
    Serial.println(pageStartIndex);

    pageEndIndex = layoutStrategy->layoutText(*provider, textRenderer, layoutConfig);

    Serial.print("Page end: ");
    Serial.println(pageEndIndex);
  } catch (const std::exception& e) {
    Serial.printf("Exception during layout: %s\n", e.what());
    abort();
  } catch (...) {
    Serial.println("Unknown exception during layout");
    abort();
  }

  // page indicator - now shows percentage
  {
    // If there are no more words, set percentage to 100%
    float pagePercentage = 1.0f;
    if (provider->getPercentage(pageEndIndex) < 1.0f)
      pagePercentage = provider->getPercentage();

    textRenderer.setFont(&Font16);
    String percentageIndicator = String((int)(pagePercentage * 100));
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(percentageIndicator.c_str(), 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (480 - w) / 2;
    textRenderer.setCursor(centerX, 790);
    textRenderer.print(percentageIndicator);
  }

  display.displayBuffer(EInkDisplay::FAST_REFRESH);
}

void TextViewerScreen::nextPage() {
  // Check if there are more words before advancing
  if (provider && provider->getPercentage(pageEndIndex) < 1.0f) {
    pageStartIndex = pageEndIndex;
    provider->setPosition(pageStartIndex);
    showPage();
  }
}

void TextViewerScreen::prevPage() {
  if (!provider || pageStartIndex <= 0)
    return;

  // Use the layout strategy to find the exact start of the previous page
  pageEndIndex = pageStartIndex;
  // Find where the previous page starts
  // Ensure the renderer is using the same font as used for forward layout so
  // measurements (space width, glyph advances) match between previous-page
  // calculation and normal layout.
  textRenderer.setFont(&NotoSans26);
  pageStartIndex = layoutStrategy->getPreviousPageStart(*provider, textRenderer, layoutConfig, pageEndIndex);

  // Set currentIndex to the start of the previous page
  provider->setPosition(pageStartIndex);

  // Do normal forward layout from this position
  showPage();
}

void TextViewerScreen::loadTextFromString(const String& content) {
  // Create provider for the entire content
  // Preserve the passed-in content on the object so the provider has
  // stable storage for its internal copy/operations.
  delete provider;
  loadedText = content;
  if (loadedText.length() > 0) {
    provider = new StringWordProvider(loadedText);
  } else {
    provider = nullptr;
  }

  pageStartIndex = 0;
  pageEndIndex = 0;
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
