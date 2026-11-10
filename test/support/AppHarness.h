#pragma once

// Helpers for driving the real Application in tests: a runtime with
// controllable clocks, a display that captures every frame, and button taps.
//
// Set STATS_SCREENSHOT_DIR to dump every captured frame as a PBM (raw
// 800x480 physical panel) for visual inspection.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "microreader/Application.h"
#include "microreader/Runtime.h"
#include "microreader/display/DrawBuffer.h"
#include "microreader/stats/ReadingStats.h"

namespace app_harness {

using namespace microreader;

class FakeRuntime final : public IRuntime {
 public:
  bool should_continue() const override {
    return true;
  }
  uint32_t frame_time_ms() const override {
    return 50;
  }
  void wait_next_frame() override {}
  std::optional<uint8_t> battery_percentage() const override {
    return 80;
  }
  uint32_t monotonic_ms() const override {
    return mono;
  }
  int64_t local_time() const override {
    return wall;
  }
  bool set_local_time(int64_t t) override {
    if (!settable)
      return false;
    wall = t;
    return true;
  }
  void advance_ms(uint32_t ms) {
    mono += ms;
    wall += ms / 1000;
  }

  uint32_t mono = 1000;
  int64_t wall = 0;
  bool settable = true;
};

class CaptureDisplay final : public IDisplay {
 public:
  void full_refresh(const uint8_t* pixels, RefreshMode, bool) override {
    capture(pixels);
    ++full_refreshes;
  }
  void partial_refresh(const uint8_t* new_pixels, const uint8_t*) override {
    capture(new_pixels);
  }
  void deep_sleep() override {
    ++deep_sleeps;
  }
  void capture(const uint8_t* p) {
    last.assign(p, p + DisplayFrame::kPixelBytes);
    ++frames;
    if (const char* dir = std::getenv("STATS_SCREENSHOT_DIR")) {
      char name[96];
      std::snprintf(name, sizeof(name), "/%s_%03d.pbm", tag.c_str(), frames);
      std::ofstream f(std::string(dir) + name, std::ios::binary);
      f << "P4\n" << DisplayFrame::kPanelWidth << " " << DisplayFrame::kPhysicalHeight << "\n";
      for (uint8_t b : last)
        f.put(static_cast<char>(~b));  // PBM: 1 = black; panel: 1 = white
    }
  }
  int black_pixels() const {
    int n = 0;
    for (uint8_t b : last)
      for (int i = 0; i < 8; ++i)
        n += ((b >> i) & 1) ? 0 : 1;
    return n;
  }
  // Pixel of the last frame at a portrait (Deg90) logical coordinate.
  bool black_at(int lx, int ly) const {
    const int px = ly + DisplayFrame::kPanelOffsetX;
    const int py = DisplayFrame::kPhysicalHeight - 1 - lx;
    const uint8_t byte = last[static_cast<size_t>(py) * DisplayFrame::kStride + px / 8];
    return ((byte >> (7 - (px & 7))) & 1) == 0;
  }

  std::vector<uint8_t> last;
  int frames = 0;
  int full_refreshes = 0;
  int deep_sleeps = 0;
  std::string tag = "frame";
};

inline ButtonState press(Button b) {
  ButtonState s;
  s.pressed_latch = static_cast<uint8_t>(1u << static_cast<uint8_t>(b));
  s.press_history[0] = static_cast<uint8_t>(b);
  s.press_history_count = 1;
  return s;
}

inline int64_t at(int y, int m, int d, int hh = 12, int mm = 0) {
  return days_from_civil(y, m, d) * 86400 + hh * 3600 + mm * 60;
}

// Fixture owning a data dir, a display, a runtime and an Application.
class AppFixture : public ::testing::Test {
 protected:
  std::filesystem::path root_;
  std::filesystem::path data_dir_;
  std::string data_dir_str_;   // Application keeps a raw pointer to these
  std::string books_dir_str_;
  CaptureDisplay display_;
  DrawBuffer buf_{display_};
  FakeRuntime rt_;
  std::unique_ptr<Application> app_;

  void SetUp() override {
    const std::string name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
    root_ = std::filesystem::temp_directory_path() / ("app_harness_" + name);
    std::filesystem::remove_all(root_);
    data_dir_ = root_ / "data";
    std::filesystem::create_directories(data_dir_ / "cache");
    std::filesystem::create_directories(data_dir_ / "data");
    data_dir_str_ = data_dir_.string();
    display_.tag = name;
  }
  void TearDown() override {
    app_.reset();
    std::filesystem::remove_all(root_);
  }

  // Copy fixture books into <root>/books and use it as the books dir.
  void use_books(const std::vector<std::string>& fixture_names) {
    const auto books = root_ / "books";
    std::filesystem::create_directories(books);
    for (const auto& n : fixture_names)
      std::filesystem::copy_file(std::filesystem::path(TEST_FIXTURES_DIR) / n, books / n,
                                 std::filesystem::copy_options::overwrite_existing);
    books_dir_str_ = books.string();
  }
  std::string book_path(const std::string& name) const {
    return (std::filesystem::path(books_dir_str_) / name).string();
  }

  void start_app() {
    app_ = std::make_unique<Application>();
    if (!books_dir_str_.empty())
      app_->set_books_dir(books_dir_str_.c_str());
    app_->set_data_dir(data_dir_str_.c_str());
    app_->start(buf_, rt_);
    tick();  // let the main menu settle (and scan books if needed)
  }
  void tick(const ButtonState& b = ButtonState{}) {
    app_->update(b, 50, buf_, rt_);
  }
  void tap(Button b) {
    tick(press(b));
  }
  std::string top() const {
    return app_->top_screen_name();
  }
  std::string settings_text() const {
    std::ifstream f(data_dir_ / "settings");
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  }
};

}  // namespace app_harness
