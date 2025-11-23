
#include "screens/FileBrowserScreen.h"

#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>

#include "Buttons.h"

FileBrowserScreen::FileBrowserScreen(EInkDisplay& display, TextRenderer& renderer, SDCardManager& sdManager)
    : display(display), textRenderer(renderer), sdManager(sdManager) {}

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
  textRenderer.clearText();
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFont(&FreeSans18pt7b);

  textRenderer.setCursor(10, 60);
  textRenderer.print("SD Card");

  textRenderer.setFont(&FreeSans12pt7b);

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
    String path = sdFiles[sdSelectedIndex];
    Serial.printf("Selected file: %s\n", path.c_str());
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
