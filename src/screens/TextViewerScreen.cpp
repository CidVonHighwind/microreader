#include "screens/TextViewerScreen.h"

#include <Arduino.h>
#include <Fonts/Font16.h>
#include <Fonts/NotoSans26.h>

#include <cstring>

#include "../SDCardManager.h"
#include "Buttons.h"
#include "text view/FileWordProvider.h"
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
  layoutConfig.marginTop = 40;
  layoutConfig.marginBottom = 20;
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

void TextViewerScreen::begin() {
  // Load persisted viewer settings (last opened file, layout) if present
  loadSettingsFromFile();
}

void TextViewerScreen::loadSettingsFromFile() {
  if (!sdManager.ready())
    return;

  char buf[512];
  size_t r = sdManager.readFileToBuffer("/textviewer_state.txt", buf, sizeof(buf));
  if (r == 0)
    return;

  // Ensure null-termination
  buf[sizeof(buf) - 1] = '\0';

  // First line: last opened file path
  char* nl = strchr(buf, '\n');
  String savedPath = String("");
  char* secondLine = nullptr;
  if (nl) {
    *nl = '\0';
    if (strlen(buf) > 0)
      savedPath = String(buf);
    secondLine = nl + 1;
  } else {
    // Only single line present -> treat as path
    if (strlen(buf) > 0)
      savedPath = String(buf);
  }

  // If there's a second line, parse layout config first so it applies
  // before we open the saved file and layout pages.
  if (secondLine && strlen(secondLine) > 0) {
    // Copy to temp buffer for strtok
    char tmp[256];
    strncpy(tmp, secondLine, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char* tok = strtok(tmp, ",");
    int values[9];
    int idx = 0;
    while (tok && idx < 9) {
      values[idx++] = atoi(tok);
      tok = strtok(nullptr, ",");
    }
    if (idx >= 1)  // alignment at minimum
      layoutConfig.alignment = static_cast<LayoutStrategy::TextAlignment>(values[0]);
    // if (idx >= 2)
    //   layoutConfig.marginLeft = values[1];
    // if (idx >= 3)
    //   layoutConfig.marginRight = values[2];
    // if (idx >= 4)
    //   layoutConfig.marginTop = values[3];
    // if (idx >= 5)
    //   layoutConfig.marginBottom = values[4];
    // if (idx >= 6)
    //   layoutConfig.lineHeight = values[5];
    // if (idx >= 7)
    //   layoutConfig.minSpaceWidth = values[6];
    // if (idx >= 8)
    //   layoutConfig.pageWidth = values[7];
    // if (idx >= 9)
    //   layoutConfig.pageHeight = values[8];
  }

  // If a saved path exists, record it for lazy opening when the screen is
  // actually shown. This preserves `begin()` as an init-only call and avoids
  // drawing during initialization.
  if (savedPath.length() > 0) {
    pendingOpenPath = savedPath;
  }
}

void TextViewerScreen::saveSettingsToFile() {
  if (!sdManager.ready())
    return;

  // First line: current file path (may be empty)
  String content = currentFilePath + "\n";

  // Second line: comma-separated layout config values
  content += String(static_cast<int>(layoutConfig.alignment)) + "," + String(layoutConfig.marginLeft) + "," +
             String(layoutConfig.marginRight) + "," + String(layoutConfig.marginTop) + "," +
             String(layoutConfig.marginBottom) + "," + String(layoutConfig.lineHeight) + "," +
             String(layoutConfig.minSpaceWidth) + "," + String(layoutConfig.pageWidth) + "," +
             String(layoutConfig.pageHeight);

  if (!sdManager.writeFile("/textviewer_state.txt", content)) {
    Serial.println("TextViewerScreen: Failed to write textviewer_state.txt");
  }
}

void TextViewerScreen::activate() {
  pageStartIndex = 0;
  // If a file was pending to open from settings, open it now (first time the
  // screen becomes active) so showing happens from an explicit show() path.
  if (pendingOpenPath.length() > 0 && currentFilePath.length() == 0) {
    String toOpen = pendingOpenPath;
    pendingOpenPath = String("");
    openFile(toOpen);
  }

  showPage();
}

// Ensure member function is in class scope
void TextViewerScreen::handleButtons(Buttons& buttons) {
  if (buttons.wasPressed(Buttons::BACK)) {
    // Save current position for the opened book (if any) before leaving
    savePositionToFile();
    saveSettingsToFile();
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
  if (!provider) {
    // No provider available (no file open). Show a helpful message instead
    // of returning silently so the user knows why nothing is displayed.
    display.clearScreen(0xFF);
    textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
    textRenderer.setFont(&NotoSans26);
    const char* msg = "No document open";
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (480 - w) / 2;
    int16_t centerY = (800 - h) / 2;
    textRenderer.setCursor(centerX, centerY);
    textRenderer.print(msg);
    display.displayBuffer(EInkDisplay::FAST_REFRESH);
    return;
  }

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
  // Clear any associated file path when loading from memory
  currentFilePath = String("");
}

void TextViewerScreen::openFile(const String& sdPath) {
  if (!sdManager.ready()) {
    Serial.println("TextViewerScreen: SD not ready; cannot open file.");
    return;
  }

  // Use a buffered file-backed provider to avoid allocating the entire file in RAM.
  delete provider;
  provider = nullptr;
  currentFilePath = sdPath;
  FileWordProvider* fp = new FileWordProvider(sdPath.c_str());
  if (!fp->isValid()) {
    Serial.printf("TextViewerScreen: failed to open %s\n", sdPath.c_str());
    delete fp;
    currentFilePath = String("");
    return;
  }

  provider = fp;
  pageStartIndex = 0;
  pageEndIndex = 0;
  // Load a saved position from SD if present
  loadPositionFromFile();
  provider->setPosition(pageStartIndex);
  showPage();
}

void TextViewerScreen::savePositionToFile() {
  if (currentFilePath.length() == 0 || !provider)
    return;
  // Build pos file name by appending ".pos" to path
  String posPath = currentFilePath + String(".pos");
  int idx = provider->getCurrentIndex();
  String content = String(idx);
  if (!sdManager.writeFile(posPath.c_str(), content)) {
    Serial.printf("Failed to save position for %s\n", currentFilePath.c_str());
  }
}

void TextViewerScreen::loadPositionFromFile() {
  if (currentFilePath.length() == 0 || !provider)
    return;
  String posPath = currentFilePath + String(".pos");
  char buf[32];
  size_t r = sdManager.readFileToBuffer(posPath.c_str(), buf, sizeof(buf));
  if (r > 0) {
    int saved = atoi(buf);
    if (saved < 0)
      saved = 0;
    provider->setPosition(saved);
    pageStartIndex = saved;
  } else {
    pageStartIndex = 0;
  }
}

void TextViewerScreen::shutdown() {
  // Persist the current position for the opened file (if any)
  savePositionToFile();
  saveSettingsToFile();
}
