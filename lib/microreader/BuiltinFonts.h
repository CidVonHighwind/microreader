#pragma once

// Reader fonts built into the firmware, in the order Settings → Font offers
// them (before any fonts from the memory card's "fonts" folder). The name is
// what the settings file stores. Each platform maps it to its font data:
// platforms/esp32/font_manager.h (X4 and X3) and platforms/desktop/main.cpp.

#include <string>

namespace microreader {

inline constexpr const char* kBuiltinFonts[] = {"Bookerly", "Alegreya", "Literata", "Atkinson Hyperlegible"};
inline constexpr int kNumBuiltinFonts = sizeof(kBuiltinFonts) / sizeof(kBuiltinFonts[0]);

inline bool is_builtin_font(const std::string& name) {
  for (const char* f : kBuiltinFonts)
    if (name == f)
      return true;
  return false;
}

}  // namespace microreader
