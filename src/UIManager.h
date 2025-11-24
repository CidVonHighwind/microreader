#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <memory>
#include <unordered_map>

#include "Buttons.h"
#include "EInkDisplay.h"
#include "TextRenderer.h"
#include "screens/Screen.h"
#include "screens/text view/TextLayout.h"

class SDCardManager;

// Forward-declare concrete screen types (global, not nested)
class FileBrowserScreen;
class ImageViewerScreen;
class TextViewerScreen;

// Hash function for enum class
struct EnumClassHash {
  template <typename T>
  std::size_t operator()(T t) const {
    return static_cast<std::size_t>(t);
  }
};

class UIManager {
 public:
  // Typed screen identifiers so callers don't use raw indices
  enum class ScreenId { FileBrowser, ImageViewer, TextViewer };

  // Constructor
  UIManager(EInkDisplay& display, class SDCardManager& sdManager);

  void begin();
  void handleButtons(Buttons& buttons);
  void showSleepScreen();

  // Show a screen by id
  void showScreen(ScreenId id);

  // Open a text file (path on SD) in the text viewer and switch to that screen.
  void openTextFile(const String& sdPath);

 private:
  EInkDisplay& display;
  SDCardManager& sdManager;
  TextRenderer textRenderer;
  TextLayout textLayout;

  ScreenId currentScreen = ScreenId::FileBrowser;

  // Map holding owning pointers to the screens; screens are
  // constructed in the .cpp ctor and live for the UIManager lifetime.
  std::unordered_map<ScreenId, std::unique_ptr<Screen>, EnumClassHash> screens;

  // Order for navigation
  std::vector<ScreenId> screenOrder = {ScreenId::FileBrowser, ScreenId::ImageViewer, ScreenId::TextViewer};

  ScreenId nextScreenId(ScreenId cur) const;
  ScreenId prevScreenId(ScreenId cur) const;
};

#endif
