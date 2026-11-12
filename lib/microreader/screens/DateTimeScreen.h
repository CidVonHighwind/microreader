#pragma once

#include <cstdint>
#include <optional>

#include "../Input.h"
#include "../display/DrawBuffer.h"
#include "IScreen.h"

namespace microreader {

// Sets the device's wall clock (local time), used to date reading statistics.
//
// Fields: day, month, year, hour, minute.
//   ▲ / ▼ (front, per Menu Nav) or side Up / Down   change the focused field
//   ▶ (Button1)  next field; on the last field, save and exit
//   ◀ (Button0)  previous field; on the first field, exit without saving
class DateTimeScreen final : public IScreen {
 public:
  enum Field : int { kDay = 0, kMonth, kYear, kHour, kMinute, kNumFields };
  static constexpr int kMinYear = 2024;
  static constexpr int kMaxYear = 2099;

  const char* name() const override {
    return "Date & Time";
  }

  void start(DrawBuffer& buf, IRuntime& runtime) override;
  void stop() override {}
  void update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;

  // Test accessors.
  int field() const {
    return field_;
  }
  int value(Field f) const {
    return values_[f];
  }
  int64_t composed_local_time() const;

 private:
  void adjust_(int delta);
  void clamp_day_();
  void draw_(DrawBuffer& buf, std::optional<uint8_t> battery) const;

  int values_[kNumFields] = {1, 1, kMinYear, 0, 0};
  int field_ = kDay;
  bool save_failed_ = false;  // platform refused set_local_time()
};

}  // namespace microreader
