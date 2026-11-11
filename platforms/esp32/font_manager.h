#pragma once

#include <cstring>

#include "asset_blob.h"
#include "esp_log.h"
#include "font_partition.h"
#include "microreader/Application.h"
#include "microreader/FontManager.h"

// ESP32 font manager: extends the core FontManager with spiffs partition
// provisioning.  Declare as `static FontManager font_mgr(app)` in app_main
// to keep objects in BSS (not on the stack).
//
// Font bundles live in the appended asset blob (see asset_blob.h) so they
// don't consume DROM at boot.  We mmap on demand only during provisioning
// (~5 s, once per firmware update), then unmap so the MMU pages are free.
class FontManager : public microreader::FontManager {
 public:
  explicit FontManager(microreader::Application& app) : app_(app) {}

  // Returns the asset name for the currently selected built-in font, or
  // nullptr if the user selected a custom (SD-card) font.
  static const char* embedded_asset_for(const std::string& font_name) {
    if (font_name == "Alegreya")
      return "alegreya.bin";
    if (font_name == "" || font_name == "Bookerly")
      return "bookerly.bin";
    return nullptr;
  }

  // Returns true if the file is a valid FNTS v2 bundle (the current format).
  // Version 2 bundles always contain MBF4 v4 inner fonts, so no inner check needed.
  static bool is_valid_font_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f)
      return false;
    uint8_t hdr[6] = {};
    bool ok = fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr) && memcmp(hdr, "FNTS", 4) == 0 && hdr[4] > 0 &&  // num_sizes
              hdr[5] == 2;  // bundle version must be exactly 2
    fclose(f);
    return ok;
  }

#ifdef MICROREADER_X3
  // X3: the built-in fonts ship uncompressed inside the firmware and are read
  // straight from flash. The X3's partition table usually has only a 1 MB
  // "spiffs" partition, too small to unpack them into like the X4 does; that
  // partition is used only for fonts copied from the SD card, when they fit.
  static const char* builtin_asset_x3_(const std::string& font_name) {
    if (font_name == "Alegreya")
      return "alegreya.fnt";
    if (font_name == "" || font_name == "Bookerly")
      return "bookerly.fnt";
    return nullptr;
  }

  // Load a built-in font from the firmware. True if it's now in use.
  bool use_builtin_x3_(const char* asset) {
    if (builtin_loaded_ == asset && font_set_.valid())
      return true;
    size_t size = 0;
    esp_partition_mmap_handle_t h = 0;
    const uint8_t* data = static_cast<const uint8_t*>(asset_blob::g_assets.map(asset, size, h));
    if (!data) {
      ESP_LOGE("font", "built-in font %s missing from firmware", asset);
      return false;
    }
    load_fonts_from_(data, size);  // assets live in flash for the whole run; no unmap
    if (!font_set_.valid()) {
      ESP_LOGE("font", "built-in font %s did not load", asset);
      builtin_loaded_ = nullptr;
      return false;
    }
    builtin_loaded_ = asset;
    ESP_LOGI("font", "using built-in font %s from firmware", asset);
    return true;
  }

  void init() {
    // Settings aren't loaded yet; ensure_ready() switches to the chosen font
    // before a book opens. Start with Bookerly so the reader always has a font.
    if (use_builtin_x3_("bookerly.fnt"))
      app_.set_reader_font(font_set());
  }

  void ensure_ready(microreader::DrawBuffer& buf) override {
    const std::string custom_font = app_.custom_font_path();
    if (const char* asset = builtin_asset_x3_(custom_font)) {
      if (use_builtin_x3_(asset))
        app_.set_reader_font(font_set());
      return;
    }

    // A font file from the SD card: it has to be copied into the spiffs
    // partition first (and fit there).
    const std::string installed_font = app_.installed_font_path();
    auto fall_back = [&](const char* message) {
      if (message) {
        buf.sync_bw_ram();
        buf.show_loading(message, 0);
      }
      app_.set_custom_font_path("Bookerly");
      if (use_builtin_x3_("bookerly.fnt"))
        app_.set_reader_font(font_set());
    };
    if (custom_font == installed_font && font_part_.mmap()) {
      load_fonts_();
      if (font_set_.valid()) {
        app_.set_reader_font(font_set());
        return;
      }
    }
    if (!is_valid_font_file(custom_font.c_str())) {
      ESP_LOGE("font", "SD card font rejected (invalid header): %s", custom_font.c_str());
      fall_back("Font incompatible!");
      return;
    }
    if (!FontPartition::fits_partition(custom_font.c_str())) {
      ESP_LOGE("font", "SD card font too large for this reader's font space: %s", custom_font.c_str());
      fall_back("Font too large!");
      return;
    }
    buf.sync_bw_ram();
    buf.show_loading("Installing fonts...", 0);
    ESP_LOGI("font", "Provisioning font from SD card: %s", custom_font.c_str());
    const bool ok = FontPartition::provision_uncompressed_file(
        custom_font.c_str(), buf.scratch_buf2(), microreader::DrawBuffer::kBufSize,
        [&buf](int pct) { buf.show_loading("Installing fonts...", pct); });
    buf.reset_after_scratch();
    if (ok && font_part_.mmap()) {
      load_fonts_();
      if (font_set_.valid()) {
        app_.set_installed_font_path(custom_font);
        app_.set_reader_font(font_set());
        return;
      }
    }
    ESP_LOGE("font", "SD card font install failed: %s", custom_font.c_str());
    fall_back("Font install failed!");
  }
#else
  void init() {
    if (font_part_.mmap()) {
      load_fonts_();
      if (font_set_.valid()) {
        for (int i = 0; i < microreader::kMaxFontSizes; i++) {
          if (prop_fonts_[i].valid()) {
            ESP_LOGI("font", "Size %d: %u glyphs, height=%u baseline=%u", i, (unsigned)prop_fonts_[i].num_glyphs(),
                     (unsigned)prop_fonts_[i].glyph_height(), (unsigned)prop_fonts_[i].baseline());
          }
        }
        app_.set_reader_font(font_set());
        const char* target_asset = embedded_asset_for(app_.custom_font_path());
        if (target_asset) {
          uint32_t expected_crc = asset_blob::g_assets.crc(target_asset);
          if (FontPartition::needs_provisioning(expected_crc)) {
            ESP_LOGI("font", "font needs provisioning \u2014 will install before app start");
          }
        }
      } else {
        ESP_LOGW("font", "no valid Normal font found");
      }
    }
  }

  // Called by ReaderScreen before opening a book (IFontEnsurer interface).
  // No-op if fonts are already provisioned.
  void ensure_ready(microreader::DrawBuffer& buf) override {
    const std::string& custom_font = app_.custom_font_path();
    const std::string& installed_font = app_.installed_font_path();

    const char* target_asset = embedded_asset_for(custom_font);
    bool is_embedded = (target_asset != nullptr);
    uint32_t target_crc = is_embedded ? asset_blob::g_assets.crc(target_asset) : 0;

    if (custom_font == installed_font && (!is_embedded || !FontPartition::needs_provisioning(target_crc))) {
      if (font_set_.valid()) {
        app_.set_reader_font(font_set());
        return;
      }
      // Font is marked installed but invalid — clear the record and fall through to re-provision.
      ESP_LOGW("font", "installed font invalid, re-provisioning");
      app_.set_installed_font_path("");
    }

    if (!is_embedded) {
      if (!is_valid_font_file(custom_font.c_str())) {
        ESP_LOGE("font", "SD card font rejected (invalid header): %s", custom_font.c_str());
        buf.sync_bw_ram();
        buf.show_loading("Font incompatible!", 0);
        app_.set_custom_font_path(installed_font);
        if (font_set_.valid())
          app_.set_reader_font(font_set());
        return;
      }
      if (!FontPartition::fits_partition(custom_font.c_str())) {
        ESP_LOGE("font", "SD card font too large for partition: %s", custom_font.c_str());
        buf.sync_bw_ram();
        buf.show_loading("Font too large!", 0);
        app_.set_custom_font_path(installed_font);
        if (font_set_.valid())
          app_.set_reader_font(font_set());
        return;
      }
    }

    buf.sync_bw_ram();
    buf.show_loading("Installing fonts...", 0);

    if (is_embedded) {
      ESP_LOGI("font", "Provisioning font \"%s\" from firmware...", target_asset);

      size_t mapped_size = 0;
      esp_partition_mmap_handle_t mmap_h = 0;
      const uint8_t* data = static_cast<const uint8_t*>(asset_blob::g_assets.map(target_asset, mapped_size, mmap_h));
      if (!data) {
        ESP_LOGE("font", "failed to map asset %s", target_asset);
        return;
      }
      bool ok = FontPartition::provision_embedded(
          data, mapped_size, target_crc, buf.scratch_buf1(), microreader::DrawBuffer::kBufSize, buf.scratch_buf2(),
          microreader::DrawBuffer::kBufSize, [&buf](int pct) { buf.show_loading("Installing fonts...", pct); });
      asset_blob::g_assets.unmap(mmap_h);

      if (ok) {
        app_.set_installed_font_path(custom_font);
        buf.reset_after_scratch();
        if (font_part_.mmap()) {
          load_fonts_();
          app_.set_reader_font(font_set());
        }
      }
    } else {
      ESP_LOGI("font", "Provisioning font from SD card: %s", custom_font.c_str());
      bool prov_ok = FontPartition::provision_uncompressed_file(
          custom_font.c_str(), buf.scratch_buf2(), microreader::DrawBuffer::kBufSize,
          [&buf](int pct) { buf.show_loading("Installing fonts...", pct); });
      buf.reset_after_scratch();
      if (prov_ok) {
        if (font_part_.mmap()) {
          load_fonts_();
          if (font_set_.valid()) {
            app_.set_installed_font_path(custom_font);
            app_.set_reader_font(font_set());
          } else {
            ESP_LOGE("font", "SD card font loaded but produced no valid Normal style");
            app_.set_custom_font_path(installed_font);
          }
        }
      } else {
        ESP_LOGE("font", "SD card font provisioning failed: %s", custom_font.c_str());
        buf.show_loading("Font install failed!", 0);
        app_.set_custom_font_path(installed_font);
        if (font_set_.valid())
          app_.set_reader_font(font_set());
      }
    }
  }

#endif  // MICROREADER_X3

  // Call in the main loop when g_font_uploaded is true (serial upload).
  void on_serial_upload() {
    if (font_part_.mmap()) {
      load_fonts_();
      if (font_set_.valid()) {
        ESP_LOGI("font", "re-loaded fonts after upload");
        app_.set_reader_font(font_set());
      }
    }
  }

 private:
  void load_fonts_() {
    load_fonts_from_(font_part_.data, font_part_.size);
#ifdef MICROREADER_X3
    builtin_loaded_ = nullptr;  // now showing the partition's font
#endif
  }

  void load_fonts_from_(const uint8_t* d, size_t sz) {
    for (auto& f : prop_fonts_)
      f = microreader::BitmapFont();
    font_set_ = microreader::BitmapFontSet();
    num_fonts_ = 0;
    if (!d) {
      ESP_LOGE("font", "no font data");
      return;
    }

    if (sz < 40 || memcmp(d, "FNTS", 4) != 0 || d[5] != 2) {
      ESP_LOGE("font", "Invalid font partition (expected FNTS v2 bundle, got version %u)", d[5]);
      return;
    }

    // FNTS v1: [FNTS:4][num:1][version:1][res:2][name:32][num×size:4][data...]
    uint8_t num = d[4];
    if (num > microreader::kMaxFontSizes)
      num = microreader::kMaxFontSizes;

    char font_name[33] = {};
    memcpy(font_name, d + 8, 32);
    font_name[32] = '\0';
    ESP_LOGI("font", "Bundle font: \"%s\" (v%u, %u sizes)", font_name, d[5], num);

    constexpr size_t kSizeTableOff = 8 + 32;
    uint32_t sizes[microreader::kMaxFontSizes] = {};
    for (int i = 0; i < num; i++) {
      const uint8_t* p = d + kSizeTableOff + i * 4;
      sizes[i] = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    }
    size_t off = kSizeTableOff + static_cast<size_t>(num) * 4;
    for (int i = 0; i < num; i++) {
      if (off + sizes[i] > sz)
        break;
      load_font(d + off, sizes[i]);
      off += sizes[i];
    }
  }

  microreader::Application& app_;
  FontPartition font_part_;
#ifdef MICROREADER_X3
  const char* builtin_loaded_ = nullptr;  // asset name of the built-in font in use, if any
#endif
};
