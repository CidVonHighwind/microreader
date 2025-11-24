
#include "screens/FileBrowserScreen.h"

#include <Fonts/Font16.h>
#include <Fonts/Font27.h>

#include "Buttons.h"
#include "UIManager.h"

FileBrowserScreen::FileBrowserScreen(EInkDisplay& display, TextRenderer& renderer, SDCardManager& sdManager,
                                     UIManager& uiManager)
    : display(display), textRenderer(renderer), sdManager(sdManager), uiManager(uiManager) {}

void FileBrowserScreen::begin() {
  loadFolder();
}

// Ensure member function is in class scope
void FileBrowserScreen::handleButtons(Buttons& buttons) {
  if (buttons.wasPressed(Buttons::CONFIRM)) {
    confirm();
  } else if (buttons.wasPressed(Buttons::LEFT)) {
    selectNext();
  } else if (buttons.wasPressed(Buttons::RIGHT)) {
    selectPrev();
  }
}

void FileBrowserScreen::show() {
  renderSdBrowser();
  display.displayBuffer(EInkDisplay::FAST_REFRESH);
}

void FileBrowserScreen::renderSdBrowser() {
  display.clearScreen(0xFF);
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFont(&Font27);

  textRenderer.setCursor(10, 50);
  textRenderer.print("SD Card");

  textRenderer.setFont(&Font16);

  int lineY = 84;
  int lines = SD_LINES_PER_SCREEN;
  for (int i = 0; i < lines; ++i) {
    int idx = sdScrollOffset + i;
    if (idx >= (int)sdFiles.size())
      break;

    if (idx == sdSelectedIndex) {
      textRenderer.setCursor(8, lineY);
      textRenderer.print(">");
      textRenderer.setCursor(24, lineY);
    } else {
      textRenderer.setCursor(24, lineY);
    }

    String name = sdFiles[idx];
    if (name.length() > 30)
      name = name.substring(0, 27) + "...";
    textRenderer.print(name);
    lineY += 28;
  }
}

void FileBrowserScreen::confirm() {
  if (!sdFiles.empty()) {
    String filename = sdFiles[sdSelectedIndex];
    String fullPath = String("/Microreader/") + filename;
    Serial.printf("Selected file: %s\n", fullPath.c_str());

    // Ask UI manager to open the selected file in the text viewer
    uiManager.openTextFile(fullPath);
  }
}

void FileBrowserScreen::selectNext() {
  offsetSelection(1);
}

void FileBrowserScreen::selectPrev() {
  offsetSelection(-1);
}

void FileBrowserScreen::offsetSelection(int offset) {
  if (sdFiles.empty())
    return;

  int n = (int)sdFiles.size();
  int newIndex = sdSelectedIndex + offset;
  newIndex %= n;
  if (newIndex < 0)
    newIndex += n;
  sdSelectedIndex = newIndex;

  if (sdSelectedIndex >= sdScrollOffset + SD_LINES_PER_SCREEN) {
    sdScrollOffset = sdSelectedIndex - SD_LINES_PER_SCREEN + 1;
  } else if (sdSelectedIndex < sdScrollOffset) {
    sdScrollOffset = sdSelectedIndex;
  }

  show();
}

void FileBrowserScreen::loadFolder(int maxFiles) {
  sdFiles.clear();

  if (!sdManager.ready()) {
    Serial.println("SD not ready; cannot list files.");
    return;
  }

  auto files = sdManager.listFiles("/Microreader", maxFiles);
  for (auto& name : files) {
    sdFiles.push_back(name);
  }

  sdSelectedIndex = 0;
  sdScrollOffset = 0;
}
