#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "../Input.h"
#include "../display/DrawBuffer.h"
#include "../net/WifiTransfer.h"
#include "IScreen.h"

namespace microreader {

// Wi-Fi Transfer status: starts Wi-Fi (join or hotspot), shows the address to
// open in a browser, and what's been transferred. ◀ stops Wi-Fi and returns
// to the book list. Wi-Fi turns itself off after kIdleMs without activity.
class FileTransferScreen final : public IScreen {
 public:
  static constexpr uint32_t kIdleMs = 10u * 60u * 1000u;

  const char* name() const override {
    return "Transfer";
  }

  // Configure before pushing.
  void set_join(std::string ssid, std::string password);
  void set_hotspot();

  void start(DrawBuffer& buf, IRuntime& runtime) override;
  void stop() override;
  void update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;

  // Test accessors.
  bool hotspot() const {
    return hotspot_;
  }
  const std::string& hotspot_password() const {
    return hotspot_password_;
  }
  bool idle_stopped() const {
    return idle_stopped_;
  }

 private:
  void draw_(DrawBuffer& buf, std::optional<uint8_t> battery) const;

  bool hotspot_ = false;
  std::string ssid_;
  std::string password_;
  std::string hotspot_password_;
  WifiState last_state_ = WifiState::Off;
  uint32_t last_activity_count_ = 0;
  uint32_t last_activity_ms_ = 0;
  uint64_t index_generation_ = 0;
  int transfers_ = 0;
  bool remembered_ = false;
  bool idle_stopped_ = false;
};

}  // namespace microreader
