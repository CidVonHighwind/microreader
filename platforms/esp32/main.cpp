#include <cstdio>

#include "asset_blob.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "epd.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "font_manager.h"
#include "input.h"
#include "microreader/Application.h"
#include "microreader/HeapLog.h"
#include "microreader/Loop.h"
#include "microreader/content/Book.h"
#include "microreader/content/BookIndex.h"
#include "microreader/content/mrb/MrbConverter.h"
#include "microreader/display/DrawBuffer.h"
#include "runtime.h"
#include "sdcard.h"
#include "serial_communication.h"
#include "wifi_transfer.h"

static void verify_ota() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      esp_ota_mark_app_valid_cancel_rollback();
    }
  }
}

// When the device boots on battery (no USB), require the power button to be
// held for at least kPowerWakeupMs milliseconds before allowing boot.
// A brief accidental touch goes back to sleep immediately without any display
// activity, just like the original microreader firmware.
// Exception: software resets (e.g. after esptool flash) boot immediately.
static constexpr gpio_num_t kPowerPin = GPIO_NUM_3;
static constexpr uint32_t kPowerWakeupMs = 250;

static void verify_wakeup_press() {
#ifndef QEMU_BUILD
  // If USB is connected (GPIO20/U0RXD reads HIGH), boot immediately.
  gpio_set_direction(GPIO_NUM_20, GPIO_MODE_INPUT);
  if (gpio_get_level(GPIO_NUM_20) == 1)
    return;

  // Only require a hold check on a clean power-on (battery, no USB).
  // Crashes, panics, watchdog resets, SW resets — all boot immediately.
  if (esp_reset_reason() != ESP_RST_POWERON) {
    ESP_LOGI("pwr", "Non-poweron reset (%d) — booting immediately", (int)esp_reset_reason());
    return;
  }

  gpio_config_t cfg{};
  cfg.pin_bit_mask = (1ULL << kPowerPin);
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);

  // Wait up to 2× the threshold; if the button isn't held long enough, sleep.
  const uint32_t deadline_ms = kPowerWakeupMs * 2;
  uint32_t held_ms = 0;
  for (uint32_t elapsed = 0; elapsed < deadline_ms; elapsed += 10) {
    vTaskDelay(pdMS_TO_TICKS(10));
    if (gpio_get_level(kPowerPin) == 0) {
      held_ms += 10;
      if (held_ms >= kPowerWakeupMs)
        return;  // confirmed long press — boot normally
    } else {
      held_ms = 0;  // button released, reset counter
    }
  }

  // Short press — go back to sleep; wake again on power button press.
  ESP_LOGI("pwr", "Short press on wakeup (held %lu ms) — returning to sleep", (unsigned long)held_ms);
  esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(1ULL << kPowerPin, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
#endif
}

extern "C" void app_main(void) {
  verify_ota();
  verify_wakeup_press();

  // Initialise the appended asset blob (fonts, sleep images, ...).
  // Must happen before FontManager::init() and any sleep-image rendering.
  asset_blob::g_assets.init();

  static Esp32InputSource input;
  static EInkDisplay epd;
  static Esp32Runtime runtime(50, input.get_adc_handle());
  static microreader::Application app;
  static microreader::DrawBuffer buf(epd);
  static Esp32WifiTransfer wifi;  // Wi-Fi Transfer (off until used)

#ifndef QEMU_BUILD
  // After a software reset (post-flash) wait briefly for the serial monitor.
  if (esp_reset_reason() == ESP_RST_SW) {
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
#endif

  // --- Memory audit: log heap at every stage ---
  ESP_LOGI("mem", "after static init (DrawBuffer+App etc): free=%lu largest=%lu",
           (unsigned long)esp_get_free_heap_size(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  MR_LOGI("app", "Booting up...");

#ifndef QEMU_BUILD
  epd.begin();

  ESP_LOGI("mem", "after epd.begin: free=%lu largest=%lu", (unsigned long)esp_get_free_heap_size(),
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#else
  ESP_LOGI("app", "QEMU build: skipping epd.begin()");
#endif

  // Mount SD card (shares SPI bus with display).
  if (sd_init()) {
    MR_LOGI("app", "SD card ready");

    ESP_LOGI("mem", "after sd_init: free=%lu largest=%lu", (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    // Ensure books directory exists.
    mkdir("/sdcard/books", 0775);

    // Data directory for converted books, settings, reading state.
    mkdir("/sdcard/.microreader", 0775);
    mkdir("/sdcard/.microreader/cache", 0775);
    mkdir("/sdcard/.microreader/data", 0775);

    // Register the books directory for the selection screen.
    app.set_books_dir("/sdcard");
    app.set_data_dir("/sdcard/.microreader");
  } else {
    MR_LOGI("app", "SD card not available");
  }

  serial_start();

  ESP_LOGI("mem", "after serial_start: free=%lu largest=%lu", (unsigned long)esp_get_free_heap_size(),
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  static FontManager font_mgr(app);
  font_mgr.init();
  app.set_font_manager(&font_mgr);

  ESP_LOGI("mem", "after font init: free=%lu largest=%lu", (unsigned long)esp_get_free_heap_size(),
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  app.set_invalidate_font_fn([]() { FontPartition::invalidate(); });
  app.set_wifi(&wifi);

  app.start(buf, runtime);

  ESP_LOGI("mem", "after app.start: free=%lu largest=%lu", (unsigned long)esp_get_free_heap_size(),
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  // Discard the power-button press that woke us from deep sleep.
  input.clear_button(microreader::Button::Power);

  while (runtime.should_continue() && app.running()) {
    // Suppress auto-sleep while a PC is connected over USB.
#ifndef QEMU_BUILD
    if (usb_serial_jtag_is_connected()) {
      app.keep_awake();
    }
#endif

    // Check if a new font was uploaded via serial.
    if (g_font_uploaded) {
      g_font_uploaded = false;
      font_mgr.on_serial_upload();
    }

    // Process pending index mutation (upload via EPUB magic or 'W' command,
    // delete via 'R', rename via 'N'). Single-slot SPSC queue: receiver task
    // is the producer, this loop is the consumer.
    //
    // Deferral rules:
    //   (A) Add/Rename ops use scratch buffers (Book::open) — defer when
    //       Reader is the top screen (it owns those buffers for rendering).
    //   (C) ALL ops do SD card I/O (fopen/fprintf in save/load) which shares
    //       SPI2_HOST with the display. Defer when the EPD hardware is
    //       mid-refresh (DMA reading from framebuffer). This prevents SPI
    //       contention that corrupts the display during SD card writes.
    if (g_index_op != SerialIndexOp::None) {
      const SerialIndexOp op = g_index_op;
      const bool needs_scratch = (op == SerialIndexOp::Add || op == SerialIndexOp::Rename);
      const bool reader_active = app.is_reader_active();
      const bool epd_busy = epd.is_busy();
      const bool defer = (needs_scratch && reader_active) || epd_busy;

      ESP_LOGI("main", "index_op: op=%u needs_scratch=%d reader_active=%d epd_busy=%d defer=%d",
               (unsigned)op, (int)needs_scratch, (int)reader_active, (int)epd_busy, (int)defer);

      if (defer) {
        // Leave the slot occupied; retry next iteration. The main loop
        // continues to run (UI updates, serial commands) between retries.
      } else {
        // Copy paths to locals BEFORE clearing the slot — minimizes the window
        // in which a new op would be dropped.
        char path_a[256];
        char path_b[256];
        strncpy(path_a, g_index_path_a, sizeof(path_a) - 1);
        path_a[sizeof(path_a) - 1] = '\0';
        strncpy(path_b, g_index_path_b, sizeof(path_b) - 1);
        path_b[sizeof(path_b) - 1] = '\0';
        g_index_op = SerialIndexOp::None;  // free slot before processing

        constexpr const char* kDataDir = "/sdcard/.microreader";
        const std::string index_path = std::string(kDataDir) + "/book_index.dat";
        switch (op) {
          case SerialIndexOp::Add:
            if (!microreader::BookIndex::instance().index_file(path_a, index_path, buf))
              ESP_LOGW("main", "index_file failed for %s", path_a);
            buf.reset_after_scratch();
            break;
          case SerialIndexOp::Remove:
            microreader::BookIndex::instance().remove_path(path_a, index_path);
            break;
          case SerialIndexOp::Rename:
            // Fast path: in-place rename preserves metadata + last_open_order.
            // Fallback: src wasn't indexed → index dst fresh (extracts metadata).
            if (!microreader::BookIndex::instance().rename_in_place(path_a, path_b, index_path)) {
              if (microreader::BookIndex::instance().index_file(path_b, index_path, buf))
                ESP_LOGI("main", "rename_in_place missed %s, indexed %s instead", path_a, path_b);
              buf.reset_after_scratch();
            }
            break;
          default:
            break;
        }
      }
    }

    // Check if a new grayscale LUT was uploaded via serial.
    uint8_t lut_buf[112];
    uint8_t lut_type = 0;
    if (serial_lut_take(lut_buf, &lut_type)) {
      switch (lut_type) {
        case 0:
          epd.set_grayscale_lut(lut_buf);
          ESP_LOGI("epd", "Custom grayscale LUT set via serial (type=0)");
          break;
        case 1:
          epd.set_grayscale_revert_lut(lut_buf);
          ESP_LOGI("epd", "Custom grayscale REVERT LUT set via serial (type=1)");
          break;
        default:
          ESP_LOGI("epd", "Received LUT with unknown type %u", lut_type);
          break;
      }
    }

    // Dispatch serial path commands (open book, benchmarks).
    {
      const char* cmd_path = nullptr;
      switch (serial_cmd_take(&cmd_path)) {
        case SerialCmdType::Open:
          app.auto_open_book(cmd_path, buf, runtime);
          // auto_open_book pushes the Reader and renders the page into the
          // inactive buffer, but does not commit it. We must refresh here to
          // show the book page on the display (the Application::start() path
          // has its own buf.full_refresh() after auto_open_book returns).
          buf.refresh();
          break;
        case SerialCmdType::Bench: {
          microreader::Book book;
          uint8_t* work_buf = buf.scratch_buf1();
          uint8_t* xml_buf = buf.scratch_buf2();
          int64_t t_open = esp_timer_get_time();
          microreader::EpubError err = book.open(cmd_path, work_buf, xml_buf);
          long open_ms = (long)((esp_timer_get_time() - t_open) / 1000);
          ESP_LOGI("bench", "open() returned err %d", (int)err);
          microreader::benchmark_epub_conversion(book, "/sdcard/bench_tmp.mrb", open_ms, work_buf, xml_buf);
          buf.reset_after_scratch();
          break;
        }
        case SerialCmdType::ImgBench: {
          microreader::Book book;
          book.open(cmd_path);
          microreader::benchmark_image_size_read(book, buf.scratch_buf1());
          buf.reset_after_scratch();
          break;
        }
        case SerialCmdType::ImgDecode: {
          microreader::Book book;
          book.open(cmd_path);
          microreader::benchmark_image_decode(book, buf.scratch_buf1());
          buf.reset_after_scratch();
          break;
        }
        case SerialCmdType::FlashBench: {
          // Use scratch_buf1 (48 KB) as the write pattern buffer.
          FontPartition::bench_flash(buf.scratch_buf1(), microreader::DrawBuffer::kBufSize);
          buf.reset_after_scratch();
          break;
        }
        case SerialCmdType::InvalidateFont: {
          app.set_installed_font_path("");
          app.invalidate_font();
          break;
        }
        case SerialCmdType::RenderBench: {
          app.reader()->bench_render(buf);
          break;
        }
        default:
          break;
      }
    }

    // Skip UI update during upload: prevents display SPI (SPI2_HOST) from
    // contending with SD-card fwrite() (also SPI2_HOST).
    if (g_upload_in_progress) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    strncpy(g_top_screen_name, app.top_screen_name(), sizeof(g_top_screen_name) - 1);
    g_top_screen_name[sizeof(g_top_screen_name) - 1] = '\0';

    g_ui_drawing = true;  // Wi-Fi requests wait for this before using the SD card
    microreader::run_loop_iteration(app, buf, input, runtime);
    g_ui_drawing = false;
  }

  MR_LOGI("app", "Shutting down, entering deep sleep...");

#ifndef QEMU_BUILD
  // Enter deep sleep; wake on power button press (active LOW, GPIO 3).
  esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(1ULL << kPowerPin, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
#endif  // QEMU_BUILD
}
