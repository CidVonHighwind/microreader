#include "TextViewerScreen.h"

#include <Arduino.h>
#include <resources/fonts/FontDefinitions.h>
#include <resources/fonts/FontManager.h>
#include <resources/fonts/other/MenuFontSmall.h>

#include <cstring>

#include "../../content/providers/EpubWordProvider.h"
#include "../../content/providers/FileWordProvider.h"
#include "../../content/providers/StringWordProvider.h"
#include "../../core/Buttons.h"
#include "../../core/SDCardManager.h"
#include "../../core/Settings.h"
#include "../../text/hyphenation/HyphenationStrategy.h"
#include "../../text/layout/GreedyLayoutStrategy.h"
#include "../../text/layout/KnuthPlassLayoutStrategy.h"
#include "SettingsScreen.h"

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
  layoutConfig.marginTop = 4;
  layoutConfig.marginBottom = 24;
  layoutConfig.lineHeight = 30;
  layoutConfig.lineSpacing = 4;
  layoutConfig.minSpaceWidth = 8;
  layoutConfig.pageWidth = 480;
  layoutConfig.pageHeight = 800;
  layoutConfig.alignment = LayoutStrategy::ALIGN_LEFT;
  layoutConfig.language = Language::ENGLISH;  // Default to english hyphenation

  // Set the language on the layout strategy
  layoutStrategy->setLanguage(layoutConfig.language);
}

TextViewerScreen::~TextViewerScreen() {
  delete layoutStrategy;
  delete provider;
}

void TextViewerScreen::begin() {
  // Load last opened file path if present
  Settings& s = uiManager.getSettings();
  String savedPath = s.getString(String("textviewer.lastPath"), String(""));
  if (savedPath.length() > 0) {
    pendingOpenPath = savedPath;
  }
}

void TextViewerScreen::loadSettingsFromFile() {
  // This method now just applies settings from the in-memory Settings object
  // to the layout config. Settings are loaded from file once at startup by UIManager.
  Settings& s = uiManager.getSettings();

  // Apply layout config from Settings
  int margin = 10;
  if (s.getInt(String("settings.margin"), margin)) {
    layoutConfig.marginLeft = margin;
    layoutConfig.marginRight = margin;
  }

  // Load font settings to determine base font height
  int fontFamily = 1;
  s.getInt(String("settings.fontFamily"), fontFamily);
  int fontSize = 0;
  s.getInt(String("settings.fontSize"), fontSize);

  // Map font size index to actual pixel height: 0=26, 1=28, 2=30
  int baseFontHeight = 26;
  switch (fontSize) {
    case 0:
      baseFontHeight = 26;
      break;
    case 1:
      baseFontHeight = 28;
      break;
    case 2:
      baseFontHeight = 30;
      break;
  }

  // Line height = font height + additional spacing from settings
  int lineSpacing = 4;  // Default spacing
  if (s.getInt(String("settings.lineHeight"), lineSpacing)) {
    layoutConfig.lineSpacing = lineSpacing;
    layoutConfig.lineHeight = baseFontHeight + lineSpacing;
  }

  int alignment = 0;
  if (s.getInt(String("settings.alignment"), alignment)) {
    layoutConfig.alignment = static_cast<LayoutStrategy::TextAlignment>(alignment);
  }

  int showChapterNumbersInt = 1;
  if (s.getInt(String("settings.showChapterNumbers"), showChapterNumbersInt)) {
    showChapterNumbers = (showChapterNumbersInt != 0);
  }

  int flipPageButtonsInt = 0;
  if (s.getInt(String("settings.flipPageButtons"), flipPageButtonsInt)) {
    flipPageButtons = (flipPageButtonsInt != 0);
  }
}

void TextViewerScreen::saveSettingsToFile() {
  // Only save the last opened file path
  // Layout settings are managed by SettingsScreen
  Settings& s = uiManager.getSettings();
  s.setString(String("textviewer.lastPath"), currentFilePath);

  if (!s.save()) {
    Serial.println("TextViewerScreen: Failed to write settings.cfg");
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
  // Determine which buttons correspond to next/prev based on flipPageButtons setting
  uint8_t nextBtn1 = flipPageButtons ? Buttons::RIGHT : Buttons::LEFT;
  uint8_t nextBtn2 = flipPageButtons ? Buttons::VOLUME_DOWN : Buttons::VOLUME_UP;
  uint8_t prevBtn1 = flipPageButtons ? Buttons::LEFT : Buttons::RIGHT;
  uint8_t prevBtn2 = flipPageButtons ? Buttons::VOLUME_UP : Buttons::VOLUME_DOWN;

  // Check for long press first (chapter jump) - before consuming queued presses
  const unsigned long LONG_PRESS_MS = 500;
  if (buttons.isDown(nextBtn1) || buttons.isDown(nextBtn2)) {
    uint8_t btn = buttons.isDown(nextBtn1) ? nextBtn1 : nextBtn2;
    if (buttons.getHoldDuration(btn) >= LONG_PRESS_MS) {
      buttons.clearQueuedPresses();
      jumpToNextChapter();
      return;
    }
  } else if (buttons.isDown(prevBtn1) || buttons.isDown(prevBtn2)) {
    uint8_t btn = buttons.isDown(prevBtn1) ? prevBtn1 : prevBtn2;
    if (buttons.getHoldDuration(btn) >= LONG_PRESS_MS) {
      buttons.clearQueuedPresses();
      jumpToPreviousChapter();
      return;
    }
  }

  // Now consume all queued button presses - but only act once per direction
  bool shouldGoBack = false;
  bool shouldOpenSettings = false;
  bool shouldNextPage = false;
  bool shouldPrevPage = false;

  uint8_t btn;
  while ((btn = buttons.consumeNextPress()) != Buttons::NONE) {
    switch (btn) {
      case Buttons::BACK:
        shouldGoBack = true;
        break;

      case Buttons::CONFIRM:
        shouldOpenSettings = true;
        break;

      default:
        // Check if it's a next/prev button
        if (btn == nextBtn1 || btn == nextBtn2) {
          shouldNextPage = true;
        } else if (btn == prevBtn1 || btn == prevBtn2) {
          shouldPrevPage = true;
        }
        break;
    }
  }

  // Handle navigation - only one action wins (priority: back > settings > page turn)
  if (shouldGoBack) {
    savePositionToFile();
    saveSettingsToFile();
    uiManager.showScreen(UIManager::ScreenId::FileBrowser);
    return;
  }

  if (shouldOpenSettings) {
    uiManager.showScreen(UIManager::ScreenId::Settings);
    return;
  }

  // Page navigation - next takes priority over prev if both pressed
  if (shouldNextPage) {
    nextPage();
  } else if (shouldPrevPage) {
    prevPage();
  }
}

void TextViewerScreen::show() {
  showPage();
}

void TextViewerScreen::showPage() {
  Serial.println("showPage start");

  // Apply current settings from memory to layout config
  loadSettingsFromFile();

  if (!provider) {
    // No provider available (no file open). Show a helpful message instead
    // of returning silently so the user knows why nothing is displayed.
    display.clearScreen(0xFF);

    textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
    textRenderer.setFontFamily(getCurrentFontFamily());
    textRenderer.setFontStyle(FontStyle::ITALIC);

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
  textRenderer.setFontFamily(getCurrentFontFamily());
  textRenderer.setFontStyle(FontStyle::REGULAR);

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

  // page indicator - now shows book-wide percentage
  {
    // Render to BW buffer
    textRenderer.setFrameBuffer(display.getFrameBuffer());
    textRenderer.setBitmapType(TextRenderer::BITMAP_BW);

    // Use book-wide percentage for display
    // If at end of chapter and it's the last chapter, show 100%
    float pagePercentage = provider->getPercentage();
    if (provider->getChapterPercentage(pageEndIndex) >= 1.0f) {
      // At end of current chapter - check if it's the last chapter
      if (!provider->hasChapters() || provider->getCurrentChapter() >= provider->getChapterCount() - 1) {
        pagePercentage = 1.0f;
      }
    }

    textRenderer.setFont(&MenuFontSmall);  // Always use small font for page indicator

    // Build indicator string with chapter info if available
    // Format: "Ch X/Y - Z%" or "ChapterName (X/Y) - Z%" or just "Z%"
    String indicator;
    if (provider->hasChapters() && provider->getChapterCount() > 1) {
      String chapterName = provider->getCurrentChapterName();
      if (!chapterName.isEmpty()) {
        // Truncate long chapter names
        const int maxChapterNameLength = 48;
        if (chapterName.length() > maxChapterNameLength) {
          chapterName = chapterName.substring(0, maxChapterNameLength - 3) + "...";
        }
        indicator = chapterName;
        if (showChapterNumbers) {
          int currentCh = provider->getCurrentChapter() + 1;  // 1-indexed for display
          int totalCh = provider->getChapterCount();
          indicator += " (" + String(currentCh) + "/" + String(totalCh) + ")";
        }
        indicator += " - ";
      } else if (showChapterNumbers) {
        int currentCh = provider->getCurrentChapter() + 1;  // 1-indexed for display
        int totalCh = provider->getChapterCount();
        indicator = "Ch " + String(currentCh) + "/" + String(totalCh) + " - ";
      }
    }
    indicator += String((int)(pagePercentage * 100)) + "%";

    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(indicator.c_str(), 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (480 - w) / 2;
    textRenderer.setCursor(centerX, 790);
    textRenderer.print(indicator);
  }

  // display bw parts
  display.displayBuffer(EInkDisplay::FAST_REFRESH);

  // grayscale rendering
  {
    textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
    textRenderer.setFontFamily(getCurrentFontFamily());
    textRenderer.setFontStyle(FontStyle::REGULAR);

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
  if (!provider)
    return;

  // Check if there are more words in current chapter (use chapter percentage, not book percentage)
  if (provider->getChapterPercentage(pageEndIndex) < 1.0f) {
    provider->setPosition(pageEndIndex);
    showPage();
  } else {
    // End of chapter - try to move to next chapter
    if (provider->hasChapters()) {
      int currentChapter = provider->getCurrentChapter();
      int chapterCount = provider->getChapterCount();
      if (currentChapter + 1 < chapterCount) {
        provider->setChapter(currentChapter + 1);
        pageStartIndex = 0;
        pageEndIndex = 0;
        showPage();
      }
    }
  }
}

void TextViewerScreen::prevPage() {
  if (!provider)
    return;

  // If at the beginning of current chapter, try to go to previous chapter
  if (!provider->hasPrevWord()) {
    if (provider->hasChapters()) {
      int currentChapter = provider->getCurrentChapter();
      if (currentChapter > 0) {
        // Go to previous chapter and position at the end
        provider->setChapter(currentChapter - 1);
        // Go to end of previous chapter by setting position to a large value
        // then use getPreviousPageStart to find the last page
        provider->setPosition(0x7FFFFFFF);  // Seek to end
        pageStartIndex = provider->getCurrentIndex();
        pageEndIndex = pageStartIndex;
      }
    }
    // If we can't go to previous chapter (or no chapters), do nothing
    if (!provider->hasPrevWord())
      return;
  }

  textRenderer.setFontFamily(getCurrentFontFamily());

  // Find where the previous page starts
  pageStartIndex = layoutStrategy->getPreviousPageStart(*provider, textRenderer, layoutConfig, pageStartIndex);

  // Set currentIndex to the start of the previous page
  provider->setPosition(pageStartIndex);

  // Do normal forward layout from this position
  showPage();
}

void TextViewerScreen::jumpToNextChapter() {
  if (!provider)
    return;

  if (provider->hasChapters()) {
    int currentChapter = provider->getCurrentChapter();
    int chapterCount = provider->getChapterCount();
    if (currentChapter + 1 < chapterCount) {
      // Go to next chapter
      provider->setChapter(currentChapter + 1);
      pageStartIndex = 0;
      pageEndIndex = 0;
      showPage();
      return;
    } else {
      // At last chapter - go to end of chapter
      provider->setPosition(0x7FFFFFFF);
      pageStartIndex = provider->getCurrentIndex();
    }
  } else {
    // No chapters - go to end of document
    provider->setPosition(0x7FFFFFFF);
    pageStartIndex = provider->getCurrentIndex();
  }

  // Find where the previous page starts
  pageStartIndex = layoutStrategy->getPreviousPageStart(*provider, textRenderer, layoutConfig, pageStartIndex);
  provider->setPosition(pageStartIndex);
  showPage();
}

void TextViewerScreen::jumpToPreviousChapter() {
  if (!provider)
    return;

  // If not at start, go to start first
  if (provider->hasPrevWord()) {
    provider->setPosition(0);
    pageStartIndex = 0;
    pageEndIndex = 0;
    showPage();
  } else if (provider->hasChapters()) {
    // Already at chapter start - go to previous chapter
    int currentChapter = provider->getCurrentChapter();
    if (currentChapter > 0) {
      provider->setChapter(currentChapter - 1);
      pageStartIndex = 0;
      pageEndIndex = 0;
      showPage();
    }
    // If at first chapter, already at start - do nothing
  }
  // If no chapters and at start, already at beginning - do nothing
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
  // measure time taken to open file
  unsigned long startTime = millis();

  if (!sdManager.ready()) {
    Serial.println("TextViewerScreen: SD not ready; cannot open file.");
    showErrorMessage("SD card not ready");
    return;
  }

  // Use a buffered file-backed provider to avoid allocating the entire file in RAM.
  delete provider;
  provider = nullptr;
  currentFilePath = sdPath;

  // Load the saved position from SD if present
  loadPositionFromFile();

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
      showErrorMessage("Failed to open EPUB");
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
      showErrorMessage("Failed to open file");
      return;
    }
    provider = fp;
  }

  // Set the hyphenation language based on the file type
  if (isEpub) {
    // For EPUB files, get language from the EPUB metadata
    EpubWordProvider* epubProvider = static_cast<EpubWordProvider*>(provider);
    Language epubLanguage = epubProvider->getLanguage();
    layoutStrategy->setLanguage(epubLanguage);
    Serial.printf("Set hyphenation language to %d for EPUB\n", static_cast<int>(epubLanguage));
  } else {
    // For non-EPUB files, use default English hyphenation
    layoutStrategy->setLanguage(Language::ENGLISH);
  }

  // Set chapter first (if provider supports it), then position within chapter
  unsigned long provStart = millis();
  if (provider->hasChapters() && currentChapter > 0) {
    Serial.printf("Setting chapter to %d\n", currentChapter);
    provider->setChapter(currentChapter);
  } else {
    Serial.printf("No chapters\n");
    currentChapter = 0;
    provider->setChapter(0);
  }
  provider->setPosition(pageStartIndex);
  unsigned long provMs = millis() - provStart;
  Serial.printf("  Provider setup took  %lu ms\n", provMs);

  unsigned long endTime = millis();
  Serial.printf("Opened file  %s  in  %lu ms\n", sdPath.c_str(), endTime - startTime);
}

void TextViewerScreen::savePositionToFile() {
  if (currentFilePath.length() == 0 || !provider)
    return;
  // Build pos file name by appending ".pos" to path
  String posPath = currentFilePath + String(".pos");
  int idx = provider->getCurrentIndex();
  int chapter = provider->getCurrentChapter();
  // Format: chapter,position
  String content = String(chapter) + "," + String(idx);
  if (!sdManager.writeFile(posPath.c_str(), content)) {
    Serial.printf("Failed to save position for %s\n", currentFilePath.c_str());
  }
}

void TextViewerScreen::loadPositionFromFile() {
  if (currentFilePath.length() == 0)
    return;
  String posPath = currentFilePath + String(".pos");
  char buf[64];
  size_t r = sdManager.readFileToBuffer(posPath.c_str(), buf, sizeof(buf));

  if (r > 0) {
    buf[sizeof(buf) - 1] = '\0';
    // Parse format: chapter,position (or just position for backwards compatibility)
    int chapter = 0;
    int position = 0;
    char* comma = strchr(buf, ',');
    if (comma) {
      // New format: chapter,position
      *comma = '\0';
      chapter = atoi(buf);
      position = atoi(comma + 1);
    } else {
      // Old format: just position (assume chapter 0)
      position = atoi(buf);
    }
    if (chapter < 0)
      chapter = 0;
    if (position < 0)
      position = 0;

    currentChapter = chapter;
    pageStartIndex = position;
    pageEndIndex = 0;
  } else {
    currentChapter = 0;
    pageStartIndex = 0;
    pageEndIndex = 0;
  }
}

void TextViewerScreen::shutdown() {
  // Persist the current position for the opened file (if any)
  savePositionToFile();
  saveSettingsToFile();
}

void TextViewerScreen::showErrorMessage(const char* msg) {
  display.clearScreen(0xFF);

  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFontFamily(&bookerly26Family);
  textRenderer.setFontStyle(FontStyle::ITALIC);

  int16_t x1, y1;
  uint16_t w, h;
  textRenderer.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  int16_t centerX = (480 - w) / 2;
  int16_t centerY = (800 - h) / 2;
  textRenderer.setCursor(centerX, centerY);
  textRenderer.print(msg);

  display.displayBuffer(EInkDisplay::FAST_REFRESH);
}
