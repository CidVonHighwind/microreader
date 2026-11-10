#pragma once

// Interface between the Wi-Fi Transfer screens and the platform's Wi-Fi.
// The ESP32 implementation lives in platforms/esp32/wifi_transfer.h; the
// desktop emulator has none (the screens say Wi-Fi isn't available).

#include <cstdint>
#include <string>
#include <vector>

namespace microreader {

constexpr const char* kWifiHostname = "sami";          // → http://sami.local
constexpr const char* kWifiHotspotName = "Sami Reader";

enum class WifiState : uint8_t {
  Off,
  Connecting,  // joining a network or starting the hotspot
  Connected,   // joined a network; web server running
  Hotspot,     // own network running; web server running
  Failed,      // see failure()
};

struct WifiNetwork {
  std::string ssid;
  int rssi = 0;  // dBm
  bool secured = true;
};

class IWifiTransfer {
 public:
  virtual ~IWifiTransfer() = default;

  // Look for networks (blocking, a few seconds). Sorted strongest first.
  virtual bool scan(std::vector<WifiNetwork>& out) = 0;
  // Join a network / start the hotspot, then serve the web page. Non-blocking:
  // watch state().
  virtual bool start_join(const std::string& ssid, const std::string& password) = 0;
  virtual bool start_hotspot(const std::string& ssid, const std::string& password) = 0;
  // Stop the web server and turn the radio off.
  virtual void stop() = 0;
  // Called every frame from the main loop while the transfer screen is open.
  virtual void poll() {}

  virtual WifiState state() const = 0;
  virtual std::string ip_address() const = 0;
  // Why the last attempt failed, in words ("Wrong password?").
  virtual std::string failure() const = 0;
  // The last join failed because the password was rejected.
  virtual bool failed_auth() const {
    return false;
  }
  // Latest thing that happened ("Received Piranesi.epub (1.2 MB)") and a
  // counter that increases each time it changes.
  virtual std::string activity() const = 0;
  virtual uint32_t activity_count() const = 0;
};

}  // namespace microreader
