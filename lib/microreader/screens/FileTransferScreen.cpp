#include "FileTransferScreen.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "../Application.h"
#include "../content/BookIndex.h"
#include "ListMenuScreen.h"

namespace microreader {

namespace {

using UiFont = ListMenuScreen::UiFont;
const BitmapFont& ui(UiFont f) {
  return ListMenuScreen::shared_font(f);
}
int text_w(const BitmapFont& f, std::string_view s) {
  return f.word_width(s.data(), s.size(), FontStyle::Regular);
}
// Shortened with "..." (on a UTF-8 boundary) to fit max_w pixels.
std::string fit(const BitmapFont& f, std::string s, int max_w) {
  if (text_w(f, s) <= max_w)
    return s;
  while (!s.empty() && text_w(f, s + "...") > max_w) {
    s.pop_back();
    while (!s.empty() && (static_cast<uint8_t>(s.back()) & 0xC0) == 0x80)
      s.pop_back();  // continuation bytes
    if (!s.empty() && static_cast<uint8_t>(s.back()) >= 0xC0)
      s.pop_back();  // a lead byte left without its continuation
  }
  return s + "...";
}

// Centred text; returns the y below it.
int line(DrawBuffer& buf, const BitmapFont& f, int cx, int top, std::string_view s, int gap = 6) {
  buf.draw_text_proportional(cx - text_w(f, s) / 2, top + f.baseline(), s.data(), s.size(), f, false);
  return top + f.y_advance() + gap;
}

}  // namespace

void FileTransferScreen::set_join(std::string ssid, std::string password) {
  hotspot_ = false;
  ssid_ = std::move(ssid);
  password_ = std::move(password);
}

void FileTransferScreen::set_hotspot() {
  hotspot_ = true;
  ssid_ = kWifiHotspotName;
  // A fresh 8-digit password each time, shown on screen.
  char pw[12];
  std::snprintf(pw, sizeof(pw), "%04d%04d", std::rand() % 10000, std::rand() % 10000);
  hotspot_password_ = pw;
}

void FileTransferScreen::start(DrawBuffer& buf, IRuntime& runtime) {
  transfers_ = 0;
  remembered_ = false;
  idle_stopped_ = false;
  last_activity_ms_ = runtime.monotonic_ms();
  index_generation_ = BookIndex::instance().generation();
  IWifiTransfer* wifi = app_ ? app_->wifi() : nullptr;
  if (wifi) {
    if (hotspot_)
      wifi->start_hotspot(ssid_, hotspot_password_);
    else
      wifi->start_join(ssid_, password_);
    last_activity_count_ = wifi->activity_count();
    last_state_ = wifi->state();
  }
  draw_(buf, runtime.battery_percentage());
}

void FileTransferScreen::stop() {
  if (app_ && app_->wifi())
    app_->wifi()->stop();
  password_.clear();
}

void FileTransferScreen::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) {
  Button btn;
  while (buttons.next_press(btn)) {
    if (btn == Button::Button0) {
      if (!app_)
        return;
      // stop() turns Wi-Fi off. After a failed join, go back to the network
      // list to try again; otherwise straight to the (updated) book list.
      IWifiTransfer* w = app_->wifi();
      const bool failed = !idle_stopped_ && w && w->state() == WifiState::Failed && !hotspot_;
      if (failed)
        app_->pop_screen();
      else
        app_->pop_to_root();
      return;
    }
  }
  IWifiTransfer* wifi = app_ ? app_->wifi() : nullptr;
  if (!wifi)
    return;
  wifi->poll();

  bool redraw = false;
  const uint32_t now = runtime.monotonic_ms();
  const WifiState st = wifi->state();
  if (st != last_state_) {
    if (st == WifiState::Connected && !hotspot_ && !remembered_) {
      app_->saved_wifi().remember(ssid_, password_);  // worked: no need to type it again
      remembered_ = true;
    }
    if (st == WifiState::Failed && !hotspot_ && wifi->failed_auth())
      app_->saved_wifi().forget(ssid_);  // ask for the password next time
    last_state_ = st;
    last_activity_ms_ = now;
    redraw = true;
  }
  if (wifi->activity_count() != last_activity_count_) {
    last_activity_count_ = wifi->activity_count();
    last_activity_ms_ = now;
    const std::string a = wifi->activity();
    if (a.rfind("Received", 0) == 0)
      ++transfers_;
    redraw = true;
  }

  const bool running = st == WifiState::Connected || st == WifiState::Hotspot || st == WifiState::Connecting;
  if (running && now - last_activity_ms_ >= kIdleMs && last_activity_ms_ != 0) {
    // Nobody's used it for a while: save the battery.
    wifi->stop();
    idle_stopped_ = true;
    last_state_ = WifiState::Off;
    redraw = true;
  } else if (running) {
    app_->keep_awake();  // the reader mustn't fall asleep mid-transfer
  }

  // A new book was indexed: that borrows the display buffers, so redraw fully.
  const bool index_changed = BookIndex::instance().generation() != index_generation_;
  index_generation_ = BookIndex::instance().generation();

  if (redraw || index_changed) {
    draw_(buf, runtime.battery_percentage());
    if (index_changed)
      buf.full_refresh();
    else
      buf.refresh();
  }
}

void FileTransferScreen::draw_(DrawBuffer& buf, std::optional<uint8_t> battery) const {
  const int W = buf.width();
  const int H = buf.height();
  const bool landscape = W > H;
  const int cx = landscape ? (W - 24) / 2 : W / 2;
  buf.fill(true);

  const BitmapFont& header = ui(UiFont::Header);
  const BitmapFont& large = ui(UiFont::Large);
  const BitmapFont& medium = ui(UiFont::Medium);
  const BitmapFont& small = ui(UiFont::Small);

  int y = landscape ? 10 : 15;
  y = line(buf, header, cx, y, "Wi-Fi Transfer", 2);
  y = line(buf, small, cx, y, "Sami Reader", 0);
  y += landscape ? 16 : 60;

  IWifiTransfer* wifi = app_ ? app_->wifi() : nullptr;
  const WifiState st = idle_stopped_ ? WifiState::Off : (wifi ? wifi->state() : WifiState::Off);
  const std::string ip = wifi ? wifi->ip_address() : std::string();
  const std::string url = std::string("http://") + kWifiHostname + ".local";

  if (!wifi) {
    y = line(buf, medium, cx, y, "Wi-Fi isn't available here.");
  } else if (idle_stopped_) {
    y = line(buf, large, cx, y, "Wi-Fi is off", 10);
    y = line(buf, small, cx, y, "It turned off after 10 minutes without activity.");
    y = line(buf, small, cx, y, "Press \xe2\x97\x80 to go back, then start it again.");
  } else if (st == WifiState::Connecting || st == WifiState::Off) {
    y = line(buf, large, cx, y,
             hotspot_ ? std::string("Starting the hotspot...") : fit(large, "Connecting to " + ssid_ + "...", W - 40),
             10);
    y = line(buf, small, cx, y, "This takes a few seconds.");
  } else if (st == WifiState::Failed) {
    y = line(buf, large, cx, y, hotspot_ ? std::string("Couldn't start the hotspot") : "Couldn't connect", 8);
    if (!hotspot_)
      y = line(buf, medium, cx, y, fit(medium, ssid_, W - 40), 10);
    const std::string why = wifi->failure();
    if (!why.empty())
      y = line(buf, medium, cx, y, why, 10);
    y = line(buf, small, cx, y,
             hotspot_ ? "Press \xe2\x97\x80 to go back." : "Press \xe2\x97\x80 to pick a network and try again.");
  } else {
    if (st == WifiState::Hotspot) {
      y = line(buf, medium, cx, y, "1. On your phone or computer, join:", 8);
      y = line(buf, header, cx, y, kWifiHotspotName, 4);
      y = line(buf, large, cx, y, "Password: " + hotspot_password_, landscape ? 12 : 30);
      y = line(buf, medium, cx, y, "2. Then open this in a browser:", 8);
    } else {
      y = line(buf, medium, cx, y, fit(medium, "Connected to " + ssid_, W - 40), 10);
      y = line(buf, medium, cx, y, "On a phone or computer on the same", 2);
      y = line(buf, medium, cx, y, "Wi-Fi, open this in a browser:", 10);
    }
    y = line(buf, header, cx, y, url, 4);
    if (!ip.empty())
      y = line(buf, medium, cx, y, "or  http://" + ip, 6);

    // Status box.
    y += landscape ? 10 : 36;
    const int box_w = 420;
    const int box_x = cx - box_w / 2;
    const int box_h = medium.y_advance() + small.y_advance() + 30;
    buf.fill_rect(box_x, y, box_w, 1, false);
    buf.fill_rect(box_x, y + box_h - 1, box_w, 1, false);
    buf.fill_rect(box_x, y, 1, box_h, false);
    buf.fill_rect(box_x + box_w - 1, y, 1, box_h, false);
    const std::string act = wifi->activity();
    const std::string shown = fit(medium, act.empty() ? std::string("Waiting for a browser...") : act, box_w - 20);
    if (transfers_ > 0) {
      line(buf, medium, cx, y + 12, shown, 4);
      char n[48];
      std::snprintf(n, sizeof(n), "%d file%s received", transfers_, transfers_ == 1 ? "" : "s");
      line(buf, small, cx, y + 18 + medium.y_advance(), n);
    } else {
      line(buf, medium, cx, y + (box_h - medium.y_advance()) / 2, shown);
    }
    y += box_h + (landscape ? 8 : 24);

    // What to do in the browser (portrait has the room).
    if (!landscape) {
      y = line(buf, small, cx, y, "In the browser you can add, download,", 2);
      y = line(buf, small, cx, y, "rename and delete files. New books show", 2);
      y = line(buf, small, cx, y, "up in your list when you leave this screen.", 2);
    }
  }

  // Footer (while Wi-Fi is on).
  const int foot = H - 26 - small.y_advance() * 2;
  if (wifi && !idle_stopped_ && st != WifiState::Failed && y < foot) {
    line(buf, small, cx, foot, "Press \xe2\x97\x80 to stop Wi-Fi.", 2);
    line(buf, small, cx, foot + small.y_advance() + 2, "It turns off by itself after 10 idle minutes.");
  }
  ListMenuScreen::draw_menu_bottom_bar(buf, ListMenuScreen::shared_menu_font(), app_ && app_->invert_menu_buttons(),
                                       battery);
}

}  // namespace microreader
