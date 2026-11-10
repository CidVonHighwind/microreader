#pragma once

#include <optional>
#include <string>

#include "../Input.h"
#include "../display/DrawBuffer.h"
#include "IScreen.h"

namespace microreader {

// On-screen keyboard (used for Wi-Fi passwords).
//
//   ▲ / ▼ (front, per Menu Nav)   previous / next key
//   Side Up / Down                 key above / below
//   ▶ (Button1)                    press the key
//   ◀ (Button0)                    delete a character; with nothing typed, cancel
//
// The bottom row has Shift (ABC/abc), Symbols (#+=), Space, Del and Done.
// The caller configures it before pushing and collects the result with
// take_result() when it resumes.
class TextInputScreen final : public IScreen {
 public:
  static constexpr int kCols = 10;
  static constexpr int kCharRows = 4;
  static constexpr int kCharKeys = kCols * kCharRows;  // 40
  enum Special { kShift = kCharKeys, kSymbols, kSpace, kDelete, kDone, kNumKeys };
  enum class Page { Lower, Upper, Symbols };

  const char* name() const override {
    return "Keyboard";
  }

  void configure(std::string title, std::string subtitle, std::string initial = "", size_t max_len = 63);
  // True (once) if the user pressed Done; `out` gets the text.
  bool take_result(std::string& out);

  void start(DrawBuffer& buf, IRuntime& runtime) override;
  void stop() override {}
  void update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;

  // The character a key types on a page, or 0 for the special keys.
  static char key_char(Page page, int key);

  // Test accessors.
  const std::string& text() const {
    return text_;
  }
  int key() const {
    return key_;
  }
  Page page() const {
    return page_;
  }

 private:
  void press_();
  void move_(int delta);      // along the keys, wrapping
  void move_row_(int delta);  // up/down a row, keeping the column
  void draw_(DrawBuffer& buf, std::optional<uint8_t> battery) const;

  std::string title_;
  std::string subtitle_;
  std::string text_;
  size_t max_len_ = 63;
  Page page_ = Page::Lower;
  int key_ = 0;
  bool done_ = false;
  bool has_result_ = false;
};

}  // namespace microreader
