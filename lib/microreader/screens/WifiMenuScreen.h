#pragma once

#include <string>
#include <vector>

#include "../net/WifiTransfer.h"
#include "ListMenuScreen.h"

namespace microreader {

// Wi-Fi Transfer (from the book list): start a hotspot, or pick a network.
// Scans when opened; networks with a remembered password connect straight
// away, others ask for the password on the keyboard screen.
class WifiMenuScreen final : public ListMenuScreen {
 public:
  const char* name() const override {
    return "Wi-Fi";
  }
  void start(DrawBuffer& buf, IRuntime& runtime) override;
  void stop() override;
  void pause() override {}  // keep the scan results while the keyboard is open
  void resume(DrawBuffer& buf, IRuntime& runtime) override;
  void update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;

  const std::vector<WifiNetwork>& networks() const {
    return networks_;
  }
  // For tests.
  using ListMenuScreen::count;
  using ListMenuScreen::get_item_label;
  using ListMenuScreen::is_separator;

 protected:
  void on_start() override;
  void on_select(int index) override;
  void on_back() override;

 private:
  void connect_(const std::string& ssid, const std::string& password);

  std::vector<WifiNetwork> networks_;
  bool scanned_ = false;
  bool scan_pending_ = false;
  int idx_hotspot_ = -1;
  int idx_first_network_ = -1;
  int idx_rescan_ = -1;
  std::string pending_ssid_;      // waiting for the keyboard
  std::string connect_ssid_;      // connect on the next update
  std::string connect_password_;
  bool connect_pending_ = false;
};

}  // namespace microreader
