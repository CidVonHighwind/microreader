// Wi-Fi Transfer on the reader: the Settings item, the network menu, the
// keyboard and the transfer screen. Driven through the real Application with
// button presses and a fake Wi-Fi.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "AppHarness.h"
#include "microreader/content/BookIndex.h"

namespace fs = std::filesystem;
using namespace microreader;
using namespace app_harness;

namespace {

class FakeWifi final : public IWifiTransfer {
 public:
  bool scan(std::vector<WifiNetwork>& out) override {
    ++scans;
    radio_on = true;
    out = networks;
    return true;
  }
  bool start_join(const std::string& ssid, const std::string& password) override {
    joined_ssid = ssid;
    joined_password = password;
    radio_on = true;
    st = WifiState::Connecting;
    return true;
  }
  bool start_hotspot(const std::string& ssid, const std::string& password) override {
    hotspot_ssid = ssid;
    hotspot_password = password;
    radio_on = true;
    st = WifiState::Connecting;
    return true;
  }
  void stop() override {
    ++stops;
    radio_on = false;
    st = WifiState::Off;
  }
  void poll() override {
    ++polls;
  }
  WifiState state() const override {
    return st;
  }
  std::string ip_address() const override {
    return st == WifiState::Hotspot ? "192.168.4.1" : st == WifiState::Connected ? "192.168.1.42" : "";
  }
  std::string failure() const override {
    return fail_text;
  }
  bool failed_auth() const override {
    return auth_failed;
  }
  std::string activity() const override {
    return last_activity;
  }
  uint32_t activity_count() const override {
    return activity_n;
  }
  void happen(const std::string& text) {
    last_activity = text;
    ++activity_n;
  }

  std::vector<WifiNetwork> networks;
  WifiState st = WifiState::Off;
  std::string joined_ssid, joined_password, hotspot_ssid, hotspot_password, fail_text, last_activity;
  bool auth_failed = false;
  bool radio_on = false;
  uint32_t activity_n = 0;
  int scans = 0, stops = 0, polls = 0;
};

class WifiScreensTest : public AppFixture {
 protected:
  FakeWifi wifi_;

  void SetUp() override {
    AppFixture::SetUp();
    wifi_.networks = {{"Home", -48, true}, {"Corner Cafe", -67, false}, {"Neighbour", -86, true}};
  }
  void start_with_wifi(bool with_wifi = true) {
    app_ = std::make_unique<Application>();
    if (!books_dir_str_.empty())
      app_->set_books_dir(books_dir_str_.c_str());
    app_->set_data_dir(data_dir_str_.c_str());
    if (with_wifi)
      app_->set_wifi(&wifi_);
    app_->start(buf_, rt_);
    tick();
  }
  // From the book list: ◀ to Settings, then its first item, "Wi-Fi Transfer".
  void open_wifi_menu() {
    ASSERT_EQ(top(), "Books");
    tap(Button::Button0);
    ASSERT_EQ(top(), "Settings");
    for (int guard = 0; guard < 40 && app_->settings()->selected_index() != 0; ++guard)
      tap(Button::Button3);
    ASSERT_EQ(app_->settings()->selected_index(), 0);
    tap(Button::Button1);
    ASSERT_EQ(top(), "Wi-Fi");
    tick();  // scans
  }
  WifiMenuScreen* wifi_menu() {
    return app_->wifi_menu();
  }
  std::vector<std::string> wifi_labels() {
    std::vector<std::string> out;
    auto* m = wifi_menu();
    for (int i = 0; i < m->count(); ++i)
      out.emplace_back(m->get_item_label(i));
    return out;
  }
  // Move the highlight to the row labelled `label`.
  void select_row(const std::string& label) {
    auto* m = wifi_menu();
    for (int guard = 0; guard < 40; ++guard) {
      if (std::string(m->get_item_label(m->selected_index())) == label)
        return;
      tap(Button::Button2);
    }
    FAIL() << "no row " << label;
  }
  // Type `s` on the keyboard (lower-case letters and digits only), then Done.
  void type_and_done(const std::string& s) {
    TextInputScreen* kb = app_->text_input();
    for (char c : s) {
      int target = -1;
      for (int k = 0; k < TextInputScreen::kCharKeys; ++k)
        if (TextInputScreen::key_char(TextInputScreen::Page::Lower, k) == c)
          target = k;
      ASSERT_GE(target, 0) << c;
      while (kb->key() != target)
        tap(kb->key() < target ? Button::Button2 : Button::Button3);
      tap(Button::Button1);
    }
    while (kb->key() != TextInputScreen::kDone)
      tap(Button::Button2);
    tap(Button::Button1);
  }
  std::string saved_wifi_file() const {
    std::ifstream f(data_dir_ / "wifi.txt");
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  }
};

}  // namespace

TEST_F(WifiScreensTest, FirstSettingsItemOpensTheWifiMenu) {
  use_books({"multi_chapter.epub"});
  start_with_wifi(false);  // like the desktop emulator: no Wi-Fi
  EXPECT_EQ(app_->main_menu()->count(), MainMenu::kTopRows + 1);  // no Wi-Fi row on the book list
  open_wifi_menu();
  const auto labels = wifi_labels();
  ASSERT_EQ(labels.size(), 2u);
  EXPECT_EQ(labels[0], "Wi-Fi isn't available here");
  tap(Button::Button0);
  ASSERT_EQ(top(), "Settings");
  EXPECT_EQ(app_->settings()->selected_index(), 0);  // back on its item
}

TEST_F(WifiScreensTest, ScansAndLabelsNetworks) {
  start_with_wifi();
  open_wifi_menu();
  EXPECT_EQ(wifi_.scans, 1);
  const std::vector<std::string> expected = {"Create a hotspot",  "",
                                             "Join a Wi-Fi network:", "Home",
                                             "Corner Cafe  \xc2\xb7  open", "Neighbour  \xc2\xb7  weak",
                                             "",                  "Search again"};
  EXPECT_EQ(wifi_labels(), expected);
  EXPECT_EQ(wifi_menu()->selected_index(), 0);

  select_row("Search again");
  tap(Button::Button1);
  tick();
  EXPECT_EQ(wifi_.scans, 2);

  // Leaving turns the radio off again.
  tap(Button::Button0);
  EXPECT_EQ(top(), "Settings");
  EXPECT_FALSE(wifi_.radio_on);
}

TEST_F(WifiScreensTest, JoinWithPasswordShowsAddressAndRemembersIt) {
  start_with_wifi();
  open_wifi_menu();
  select_row("Home");
  tap(Button::Button1);
  ASSERT_EQ(top(), "Keyboard");
  type_and_done("hunter2");
  tick();  // the menu connects on its next update
  ASSERT_EQ(top(), "Transfer");
  EXPECT_EQ(wifi_.joined_ssid, "Home");
  EXPECT_EQ(wifi_.joined_password, "hunter2");
  EXPECT_EQ(saved_wifi_file().find("Home"), std::string::npos);  // not until it works

  const int frames = display_.frames;
  wifi_.st = WifiState::Connected;
  tick();
  EXPECT_GT(display_.frames, frames);  // redrawn with the address
  EXPECT_NE(saved_wifi_file().find("Home\thunter2\n"), std::string::npos);
  EXPECT_GT(display_.black_pixels(), 2000);

  // Activity shows up on the screen.
  const int f2 = display_.frames;
  wifi_.happen("Received Piranesi.epub (1.2 MB)");
  tick();
  EXPECT_GT(display_.frames, f2);
  tick();
  EXPECT_EQ(display_.frames, f2 + 1);  // nothing new: no redraw

  // ◀ stops Wi-Fi and goes straight back to the book list.
  tap(Button::Button0);
  EXPECT_EQ(top(), "Books");
  EXPECT_FALSE(wifi_.radio_on);
}

TEST_F(WifiScreensTest, SavedAndOpenNetworksConnectWithoutTheKeyboard) {
  start_with_wifi();
  app_->saved_wifi().remember("Home", "from before");
  open_wifi_menu();
  EXPECT_EQ(wifi_labels()[3], "Home  \xc2\xb7  saved");
  select_row("Home  \xc2\xb7  saved");
  tap(Button::Button1);
  ASSERT_EQ(top(), "Transfer");
  EXPECT_EQ(wifi_.joined_password, "from before");
  tap(Button::Button0);

  open_wifi_menu();
  select_row("Corner Cafe  \xc2\xb7  open");
  tap(Button::Button1);
  ASSERT_EQ(top(), "Transfer");
  EXPECT_EQ(wifi_.joined_ssid, "Corner Cafe");
  EXPECT_EQ(wifi_.joined_password, "");
}

TEST_F(WifiScreensTest, WrongPasswordIsForgottenAndBackReturnsToTheList) {
  start_with_wifi();
  app_->saved_wifi().remember("Home", "old password");
  open_wifi_menu();
  select_row("Home  \xc2\xb7  saved");
  tap(Button::Button1);
  ASSERT_EQ(top(), "Transfer");
  wifi_.st = WifiState::Failed;
  wifi_.auth_failed = true;
  wifi_.fail_text = "Wrong password?";
  tick();
  std::string pw;
  EXPECT_FALSE(app_->saved_wifi().find("Home", pw));

  tap(Button::Button0);  // back to the networks, to try again
  ASSERT_EQ(top(), "Wi-Fi");
  EXPECT_EQ(wifi_.scans, 1);  // the list is kept, no rescan
  EXPECT_EQ(wifi_labels()[3], "Home");
  select_row("Home");
  tap(Button::Button1);
  EXPECT_EQ(top(), "Keyboard");  // asks for the password this time
  tap(Button::Button0);           // nothing typed: cancel
  EXPECT_EQ(top(), "Wi-Fi");
  tick();
  EXPECT_EQ(top(), "Wi-Fi");  // cancelling doesn't connect
}

TEST_F(WifiScreensTest, HotspotShowsNameAndPassword) {
  use_books({"multi_chapter.epub"});
  start_with_wifi();
  open_wifi_menu();
  tap(Button::Button1);  // "Create a hotspot" is highlighted first
  ASSERT_EQ(top(), "Transfer");
  EXPECT_EQ(wifi_.hotspot_ssid, "Sami Reader");
  ASSERT_EQ(wifi_.hotspot_password.size(), 8u);
  for (char c : wifi_.hotspot_password)
    EXPECT_TRUE(c >= '0' && c <= '9');
  EXPECT_EQ(app_->file_transfer()->hotspot_password(), wifi_.hotspot_password);
  wifi_.st = WifiState::Hotspot;
  tick();
  EXPECT_GT(display_.black_pixels(), 2000);
  tap(Button::Button0);
  EXPECT_EQ(top(), "Books");
  EXPECT_FALSE(wifi_.radio_on);
}

TEST_F(WifiScreensTest, NewBookAppearsInTheListAfterTransfer) {
  use_books({"multi_chapter.epub"});
  start_with_wifi();
  ASSERT_EQ(app_->main_menu()->book_count(), 1);
  open_wifi_menu();
  tap(Button::Button1);  // hotspot
  wifi_.st = WifiState::Hotspot;
  tick();

  // What the firmware does when a book arrives: copy + index it.
  const std::string book = book_path("with_cover.epub");
  fs::copy_file(fs::path(TEST_FIXTURES_DIR) / "with_cover.epub", book);
  const std::string index_path = data_dir_str_ + "/book_index.dat";
  ASSERT_TRUE(BookIndex::instance().index_file(book, index_path, buf_));
  buf_.reset_after_scratch();
  wifi_.happen("Received with_cover.epub (20 KB)");
  const int full = display_.full_refreshes;
  tick();
  EXPECT_EQ(display_.full_refreshes, full + 1);  // the index borrowed the display buffers

  tap(Button::Button0);
  ASSERT_EQ(top(), "Books");
  EXPECT_EQ(app_->main_menu()->book_count(), 2);
}

TEST_F(WifiScreensTest, StaysAwakeWhileOnAndTurnsOffWhenIdle) {
  start_with_wifi();
  open_wifi_menu();
  tap(Button::Button1);
  wifi_.st = WifiState::Hotspot;
  tick();

  // 11 minutes of frames: the reader would normally fall asleep after 10.
  for (int i = 0; i < 11 * 60 * 20; ++i) {
    tick();
    if (i % 1200 == 0)
      wifi_.happen("Listed /books");  // someone's browsing
  }
  EXPECT_TRUE(app_->running());
  EXPECT_EQ(top(), "Transfer");

  // Nobody uses it for 10 minutes: Wi-Fi turns itself off.
  rt_.advance_ms(FileTransferScreen::kIdleMs);
  tick();
  EXPECT_TRUE(app_->file_transfer()->idle_stopped());
  EXPECT_FALSE(wifi_.radio_on);
  EXPECT_EQ(top(), "Transfer");
  // ...and then the reader may sleep as usual.
  for (int i = 0; i < 10 * 60 * 20 + 5 && app_->running(); ++i)
    tick();
  EXPECT_FALSE(app_->running());
}

TEST_F(WifiScreensTest, PowerButtonTurnsWifiOff) {
  start_with_wifi();
  open_wifi_menu();
  EXPECT_TRUE(wifi_.radio_on);  // after the scan
  tap(Button::Power);
  EXPECT_FALSE(app_->running());
  EXPECT_FALSE(wifi_.radio_on);
}

// ── Keyboard ────────────────────────────────────────────────────────────────

TEST_F(WifiScreensTest, KeyboardPagesRowsAndLimits) {
  start_with_wifi();
  TextInputScreen* kb = app_->text_input();
  kb->configure("Wi-Fi password", "for Home", "", 5);
  app_->push_screen(ScreenId::TextInput);
  tick();
  ASSERT_EQ(top(), "Keyboard");
  EXPECT_EQ(kb->key(), 10);  // 'q'

  tap(Button::Button1);  // q
  tap(Button::Up);       // row above: '1'
  EXPECT_EQ(kb->key(), 0);
  tap(Button::Button1);
  tap(Button::Up);  // wraps to the bottom row: Shift (column 0)
  EXPECT_EQ(kb->key(), TextInputScreen::kShift);
  tap(Button::Button1);
  EXPECT_EQ(kb->page(), TextInputScreen::Page::Upper);
  tap(Button::Down);  // wraps to the top: '1'
  tap(Button::Down);  // 'Q'
  tap(Button::Button1);
  EXPECT_EQ(kb->text(), "q1Q");

  // Symbols page.
  while (kb->key() != TextInputScreen::kSymbols)
    tap(Button::Button2);
  tap(Button::Button1);
  EXPECT_EQ(kb->page(), TextInputScreen::Page::Symbols);
  EXPECT_EQ(TextInputScreen::key_char(TextInputScreen::Page::Symbols, 10), '!');
  tap(Button::Up);  // the #+= key spans columns 2-3: the key above is column 2
  EXPECT_EQ(kb->key(), 32);
  tap(Button::Button1);
  EXPECT_EQ(kb->text(), "q1Q^");
  // Space, then the 5-character limit.
  while (kb->key() != TextInputScreen::kSpace)
    tap(Button::Button2);
  tap(Button::Button1);
  tap(Button::Button1);
  EXPECT_EQ(kb->text(), "q1Q^ ");

  // ◀ deletes; Del does too.
  tap(Button::Button0);
  EXPECT_EQ(kb->text(), "q1Q^");
  tap(Button::Button2);  // Del
  tap(Button::Button1);
  EXPECT_EQ(kb->text(), "q1Q");
  // Every printable ASCII character is reachable.
  std::string all;
  for (auto page : {TextInputScreen::Page::Lower, TextInputScreen::Page::Upper, TextInputScreen::Page::Symbols})
    for (int k = 0; k < TextInputScreen::kCharKeys; ++k)
      all.push_back(TextInputScreen::key_char(page, k));
  for (char c = 33; c < 127; ++c)
    EXPECT_NE(all.find(c), std::string::npos) << c;
  EXPECT_GT(display_.black_pixels(), 3000);
}
