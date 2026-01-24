#ifndef FILE_BROWSER_SCREEN_H
#define FILE_BROWSER_SCREEN_H

#include <Arduino.h>

#include <vector>

#include "../../core/EInkDisplay.h"
#include "../../core/SDCardManager.h"
#include "../../rendering/TextRenderer.h"
#include "Screen.h"

class UIManager;

struct FileEntry {
  String filename;
  String displayName;
};

class FileBrowserScreen : public Screen {
 public:
  FileBrowserScreen(EInkDisplay& display, TextRenderer& renderer, SDCardManager& sdManager, UIManager& uiManager);

  void begin() override;
  void show() override;
  void activate() override;
  void handleButtons(class Buttons& buttons) override;

  void confirm();
  void selectNext();
  void selectPrev();
  void offsetSelection(int offset);

 private:
  void render();
  void loadFolder(int maxFiles = 200);
  FileEntry createFileEntry(const String& filename) const;
  String stripExtension(const String& filename) const;
  bool isSupportedFile(const String& filename) const;
  bool hasExtension(const String& filename, const char* ext) const;

  EInkDisplay& display;
  TextRenderer& textRenderer;
  SDCardManager& sdManager;
  UIManager& uiManager;

  std::vector<FileEntry> files;
  int selectedIndex = 0;
  int scrollOffset = 0;
};

#endif
