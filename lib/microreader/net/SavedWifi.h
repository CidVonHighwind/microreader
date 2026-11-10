#pragma once

// Remembered Wi-Fi networks for file transfer: <data_dir>/wifi.txt, one
// "ssid<TAB>password" per line, most recently used first.

#include <string>
#include <vector>

namespace microreader {

class SavedWifi {
 public:
  static constexpr size_t kMax = 8;

  struct Network {
    std::string ssid;
    std::string password;
  };

  void set_file(std::string path) {
    file_ = std::move(path);
  }
  void load();
  bool save() const;

  // Password for `ssid`, if remembered.
  bool find(const std::string& ssid, std::string& password) const;
  // Remember (or move to the front) and save.
  void remember(const std::string& ssid, const std::string& password);
  // Forget and save. No-op if not remembered.
  void forget(const std::string& ssid);

  const std::vector<Network>& networks() const {
    return networks_;
  }

 private:
  std::string file_;
  std::vector<Network> networks_;
};

}  // namespace microreader
