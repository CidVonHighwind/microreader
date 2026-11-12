#pragma once
// SD-card support for microreader2 on ESP32-C3.
// In normal builds: SPI SD card (SPI2_HOST shares bus with the e-ink display).
// In QEMU_BUILD:    FAT filesystem on internal flash (wear-levelling), mounted
//                   at /sdcard so all existing paths work unchanged.

#include <cstdio>

#include "esp_log.h"
#include "esp_vfs_fat.h"

#define SD_MOUNT "/sdcard"
#define SD_MAX_FILES 4

static const char* kSdTag = "sd";

#ifdef QEMU_BUILD
// -----------------------------------------------------------------------
// QEMU: mount a FAT partition stored in the internal SPI flash.
// The partition label "fat" must exist in the partition table.
// -----------------------------------------------------------------------
#include "wear_levelling.h"

static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

inline bool sd_init() {
  esp_vfs_fat_mount_config_t mnt{};
  mnt.format_if_mount_failed = true;  // format on first boot (blank flash)
  mnt.max_files = SD_MAX_FILES;
  mnt.allocation_unit_size = 0;  // let the driver choose

  esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(SD_MOUNT, "fat", &mnt, &s_wl_handle);
  if (err != ESP_OK) {
    ESP_LOGE(kSdTag, "flash FAT mount failed: %s", esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(kSdTag, "flash FAT mounted at %s", SD_MOUNT);
  return true;
}

inline bool sd_mounted() {
  return s_wl_handle != WL_INVALID_HANDLE;
}

#else  // !QEMU_BUILD
// -----------------------------------------------------------------------
// Real hardware: SPI SD card.
// -----------------------------------------------------------------------
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#define SD_CS GPIO_NUM_12
#ifdef MICROREADER_X3
// The X3's SD card is behind a power switch on GPIO 13 (the X4 powers it
// directly). It must be on before the card is mounted and stay on afterwards.
#define SD_POWER GPIO_NUM_13
#endif
static sdmmc_card_t* sd_card_ = nullptr;

inline bool sd_init() {
#ifdef MICROREADER_X3
  // Drive the power switch high BEFORE releasing any hold left over from the
  // last deep sleep, so the rail never glitches low.
  gpio_set_direction(SD_POWER, GPIO_MODE_OUTPUT);
  gpio_set_level(SD_POWER, 1);
  gpio_hold_dis(SD_POWER);
  gpio_set_level(SD_POWER, 1);
  vTaskDelay(pdMS_TO_TICKS(10));  // let the card power up
#endif
  // CS pin: default-high so the SD card stays deselected until we talk to it.
  gpio_set_direction(SD_CS, GPIO_MODE_OUTPUT);
  gpio_set_level(SD_CS, 1);

  sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev_cfg.gpio_cs = SD_CS;
  dev_cfg.host_id = SPI2_HOST;

  sdspi_dev_handle_t handle{};
  esp_err_t err = sdspi_host_init_device(&dev_cfg, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(kSdTag, "sdspi_host_init_device: %s", esp_err_to_name(err));
    return false;
  }

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = handle;
  host.max_freq_khz = 20000;  // 20 MHz — reliable for most SD cards

  esp_vfs_fat_mount_config_t mnt{};
  mnt.format_if_mount_failed = false;
  mnt.max_files = SD_MAX_FILES;
  mnt.allocation_unit_size = 8 * 1024;

  err = esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &dev_cfg, &mnt, &sd_card_);
  if (err != ESP_OK) {
    ESP_LOGE(kSdTag, "mount failed: %s", esp_err_to_name(err));
    return false;
  }

  float mb = (float)sd_card_->csd.capacity * sd_card_->csd.sector_size / (1024.0f * 1024.0f);
  ESP_LOGI(kSdTag, "SD mounted at %s (%.0f MB)", SD_MOUNT, mb);
  return true;
}

inline bool sd_mounted() {
  return sd_card_ != nullptr;
}

#endif  // QEMU_BUILD
