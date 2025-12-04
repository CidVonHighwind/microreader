#include "TextViewerScreen.h"

#include <Arduino.h>
#include <resources/fonts/Font14.h>
#include <resources/fonts/NotoSans26.h>

#include <cstring>

#include "../../core/Buttons.h"
#include "../../core/SDCardManager.h"
#include "textview/EpubWordProvider.h"
#include "textview/FileWordProvider.h"
#include "textview/GreedyLayoutStrategy.h"
#include "textview/KnuthPlassLayoutStrategy.h"
#include "textview/StringWordProvider.h"
#include "textview/hyphenation/HyphenationStrategy.h"

TextViewerScreen::TextViewerScreen(EInkDisplay& display, TextRenderer& renderer, SDCardManager& sdManager,
                                   UIManager& uiManager)
    : display(display),
      textRenderer(renderer),
      layoutStrategy(new KnuthPlassLayoutStrategy()),
      // layoutStrategy(new GreedyLayoutStrategy()),
      sdManager(sdManager),
      uiManager(uiManager) {
  // Initialize layout config
  layoutConfig.marginLeft = 10;
  layoutConfig.marginRight = 10;
  layoutConfig.marginTop = 44;
  layoutConfig.marginBottom = 20;
  layoutConfig.lineHeight = 30;
  layoutConfig.minSpaceWidth = 8;
  layoutConfig.pageWidth = 480;
  layoutConfig.pageHeight = 800;
  layoutConfig.alignment = LayoutStrategy::ALIGN_LEFT;
  layoutConfig.language = Language::GERMAN;  // Default to German hyphenation

  // Set the language on the layout strategy
  layoutStrategy->setLanguage(layoutConfig.language);
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
  size_t r = sdManager.readFileToBuffer("/Microreader/textviewer_state.txt", buf, sizeof(buf));
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

  if (!sdManager.writeFile("/Microreader/textviewer_state.txt", content)) {
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

  // print out current percentage
  Serial.print("Page start: ");
  Serial.println(provider->getCurrentIndex());

  unsigned long layoutStart = millis();
  LayoutStrategy::PageLayout layout = layoutStrategy->layoutText(*provider, textRenderer, layoutConfig);
  unsigned long layoutEnd = millis();

  Serial.print("Layout time: ");
  Serial.print(layoutEnd - layoutStart);
  Serial.println(" ms");

  pageStartIndex = provider->getCurrentIndex();
  pageEndIndex = layout.endPosition;

  unsigned long renderStart = millis();

  // Render to BW buffer
  textRenderer.setFrameBuffer(display.getFrameBuffer());
  textRenderer.setBitmapType(TextRenderer::BITMAP_BW);
  layoutStrategy->renderPage(layout, textRenderer, layoutConfig);

  unsigned long renderEnd = millis();

  Serial.print("Render time: ");
  Serial.print(renderEnd - renderStart);
  Serial.println(" ms");

  Serial.print("Page end: ");
  Serial.println(pageEndIndex);

  // page indicator - now shows percentage
  {
    // Render to BW buffer
    textRenderer.setFrameBuffer(display.getFrameBuffer());
    textRenderer.setBitmapType(TextRenderer::BITMAP_BW);

    // If there are no more words, set percentage to 100%
    float pagePercentage = 1.0f;
    if (provider->getPercentage(pageEndIndex) < 1.0f)
      pagePercentage = provider->getPercentage();

    textRenderer.setFont(&Font14);
    String percentageIndicator = String((int)(pagePercentage * 100));
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(percentageIndicator.c_str(), 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (480 - w) / 2;
    textRenderer.setCursor(centerX, 790);
    textRenderer.print(percentageIndicator);
  }

  // display bw parts
  display.displayBuffer(EInkDisplay::FAST_REFRESH);

  // grayscale rendering
  {
    textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
    textRenderer.setFont(&NotoSans26);

    // Render and copy to LSB buffer
    display.clearScreen(0x00);
    textRenderer.setFrameBuffer(display.getFrameBuffer());
    textRenderer.setBitmapType(TextRenderer::BITMAP_GRAY_LSB);
    layoutStrategy->renderPage(layout, textRenderer, layoutConfig);
    display.copyGrayscaleLsbBuffers(display.getFrameBuffer());

    // Render and copy to MSB buffer
    display.clearScreen(0x00);
    textRenderer.setFrameBuffer(display.getFrameBuffer());
    textRenderer.setBitmapType(TextRenderer::BITMAP_GRAY_MSB);
    layoutStrategy->renderPage(layout, textRenderer, layoutConfig);
    display.copyGrayscaleMsbBuffers(display.getFrameBuffer());

    // display grayscale part
    display.displayGrayBuffer();
  }
}

void TextViewerScreen::nextPage() {
  // Check if there are more words before advancing
  if (provider && provider->getPercentage(pageEndIndex) < 1.0f) {
    provider->setPosition(pageEndIndex);
    showPage();
  }
}

void TextViewerScreen::prevPage() {
  if (!provider || pageStartIndex <= 0)
    return;

  textRenderer.setFont(&NotoSans26);

  // Find where the previous page starts
  pageStartIndex = layoutStrategy->getPreviousPageStart(*provider, textRenderer, layoutConfig, pageStartIndex);

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

  // Check if this is an EPUB file
  bool isEpub = false;
  if (sdPath.length() >= 5) {
    String ext = sdPath.substring(sdPath.length() - 5);
    ext.toLowerCase();
    if (ext == String(".epub")) {
      isEpub = true;
    }
  }

  if (isEpub) {
    // Use EPUB word provider
    EpubWordProvider* ep = new EpubWordProvider(sdPath.c_str());
    if (!ep->isValid()) {
      Serial.printf("TextViewerScreen: failed to open EPUB %s\n", sdPath.c_str());
      delete ep;
      currentFilePath = String("");
      return;
    }
    provider = ep;
  } else {
    // Use regular file word provider for text files
    FileWordProvider* fp = new FileWordProvider(sdPath.c_str());
    if (!fp->isValid()) {
      Serial.printf("TextViewerScreen: failed to open %s\n", sdPath.c_str());
      delete fp;
      currentFilePath = String("");
      return;
    }
    provider = fp;
  }

  pageStartIndex = 0;
  pageEndIndex = 0;
  // Load a saved position from SD if present
  loadPositionFromFile();
  provider->setPosition(pageStartIndex);
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
