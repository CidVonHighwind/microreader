#include "WifiMenuScreen.h"

#include "../Application.h"

namespace microreader {

void WifiMenuScreen::start(DrawBuffer& buf, IRuntime& runtime) {
  if (!scanned_)
    scan_pending_ = true;  // shows "Searching…" now, scans on the next update
  ListMenuScreen::start(buf, runtime);
}

void WifiMenuScreen::stop() {
  networks_.clear();
  scanned_ = false;
  scan_pending_ = false;
  connect_pending_ = false;
  pending_ssid_.clear();
  free_items_storage();
}

void WifiMenuScreen::resume(DrawBuffer& buf, IRuntime& runtime) {
  std::string password;
  if (!pending_ssid_.empty() && app_->text_input()->take_result(password)) {
    connect_ssid_ = pending_ssid_;
    connect_password_ = password;
    connect_pending_ = true;
  }
  pending_ssid_.clear();
  if (connect_pending_)
    return;  // straight on to the transfer screen: don't flash the list first
  ListMenuScreen::start(buf, runtime);
}

void WifiMenuScreen::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) {
  if (connect_pending_) {
    connect_pending_ = false;
    connect_(connect_ssid_, connect_password_);
    return;
  }
  if (scan_pending_) {
    scan_pending_ = false;
    networks_.clear();
    if (IWifiTransfer* wifi = app_->wifi())
      wifi->scan(networks_);
    scanned_ = true;
    restart();  // rebuild with the results and redraw
    buf.refresh();
    return;
  }
  ListMenuScreen::update(buttons, buf, runtime);
}

void WifiMenuScreen::on_start() {
  title_ = "Wi-Fi Transfer";
  subtitle_ = "Send books from a phone or computer";
  idx_hotspot_ = idx_first_network_ = idx_rescan_ = -1;

  if (!app_->wifi()) {
    add_separator("Wi-Fi isn't available here");
    add_separator("(the desktop emulator has no Wi-Fi)");
    return;
  }

  idx_hotspot_ = count();
  add_item("Create a hotspot");
  add_separator();

  if (!scanned_) {
    add_separator("Searching for Wi-Fi networks...");
    return;
  }
  if (networks_.empty()) {
    add_separator("No Wi-Fi networks found");
  } else {
    add_separator("Join a Wi-Fi network:");
    idx_first_network_ = count();
    std::string saved_pw;
    for (const auto& n : networks_) {
      std::string label = n.ssid;
      if (app_->saved_wifi().find(n.ssid, saved_pw))
        label += "  \xc2\xb7  saved";
      else if (!n.secured)
        label += "  \xc2\xb7  open";
      if (n.rssi < -80)
        label += "  \xc2\xb7  weak";
      add_item(label);
    }
  }
  add_separator();
  idx_rescan_ = count();
  add_item("Search again");
}

void WifiMenuScreen::on_select(int index) {
  if (index == idx_hotspot_) {
    app_->file_transfer()->set_hotspot();
    app_->push_screen(ScreenId::FileTransfer);
    return;
  }
  if (index == idx_rescan_) {
    scanned_ = false;
    scan_pending_ = true;
    restart();
    return;
  }
  if (idx_first_network_ >= 0 && index >= idx_first_network_ &&
      index < idx_first_network_ + static_cast<int>(networks_.size())) {
    const WifiNetwork& n = networks_[static_cast<size_t>(index - idx_first_network_)];
    std::string password;
    if (!n.secured) {
      connect_(n.ssid, "");
    } else if (app_->saved_wifi().find(n.ssid, password)) {
      connect_(n.ssid, password);
    } else {
      pending_ssid_ = n.ssid;
      app_->text_input()->configure("Wi-Fi password", "for " + n.ssid);
      app_->push_screen(ScreenId::TextInput);
    }
  }
}

void WifiMenuScreen::on_back() {
  if (IWifiTransfer* wifi = app_->wifi())
    wifi->stop();  // the scan turned the radio on
  app_->pop_screen();
}

void WifiMenuScreen::connect_(const std::string& ssid, const std::string& password) {
  app_->file_transfer()->set_join(ssid, password);
  app_->push_screen(ScreenId::FileTransfer);
}

}  // namespace microreader
