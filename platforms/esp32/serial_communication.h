#pragma once

// Receives frames over USB Serial/JTAG and dispatches them.
//
// Frame types (matched by magic prefix):
//   0xDEADBEEF  LUT frame     → applied to the EPD
//   "EPUB"      file upload   → /sdcard/books/
//   "SIMG"      file upload   → /sdcard/sleep/
//   "SDFN"      file upload   → /sdcard/fonts/
//   "FONT"      font upload   → raw spiffs partition
//   "CMND"      command frame → see handle_serial_cmd()
//
// File upload format (EPUB/SIMG/SDFN):
//   [4B] magic
//   [2B LE] filename length
//   [N B]   filename
//   [4B LE] payload size
//   [data]  in 2 KB chunks; each chunk ACKed with 0x06
//   [4B LE] CRC-32 of full payload
//   Response: "READY\n" → 0x06 per chunk → "OK\n" or "ERR:...\n"
//
// Call serial_start() once from app_main.
// Poll serial_lut_take(buf) each loop for LUT data.

#include <dirent.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <queue>
#include <string>

#include "microreader/content/BookIndex.h"

#ifdef QEMU_BUILD
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#else
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#endif
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "font_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr const char* kLutRxTag = "lut_rx";
static constexpr const char* kUpTag = "upload";
static constexpr const char* kCmdTag = "cmd";  // also used by request_index_op and handle_serial_cmd
static constexpr uint8_t kLutMagic[4] = {0xDE, 0xAD, 0xBE, 0xEF};
static constexpr uint8_t kEpubMagic[4] = {'E', 'P', 'U', 'B'};
static constexpr uint8_t kSimgMagic[4] = {'S', 'I', 'M', 'G'};
static constexpr uint8_t kSdFntMagic[4] = {'S', 'D', 'F', 'N'};
static constexpr uint8_t kCmdMagic[4] = {'C', 'M', 'N', 'D'};
static constexpr uint8_t kFontMagic[4] = {'F', 'O', 'N', 'T'};
static constexpr uint32_t kLutSize = 112;
static constexpr uint32_t kLutFrameSize = 113;  // 1 byte type + 112 bytes LUT
static constexpr uint8_t kAck = 0x06;           // flow-control ACK between chunks

// LUT state: shared between receiver task and main loop.
static uint8_t g_lut_buf[kLutSize];
static uint8_t g_lut_type = 0;
static volatile bool g_lut_pending = false;

// Button injection: OR'd into next poll_buttons before clearing.
volatile uint8_t g_serial_buttons = 0;

// Single-slot command queue: only one path command can be pending at a time.
// The serial receiver task writes path then sets type as the commit signal.
// The main loop reads type, dispatches, then clears to None.
enum class SerialCmdType : uint8_t {
  None = 0,
  Open,
  Bench,
  ImgBench,
  ImgDecode,
  FlashBench,
  InvalidateFont,
  RenderBench
};
static char g_cmd_path[256];
static volatile SerialCmdType g_cmd_type = SerialCmdType::None;

// Set when a font has been uploaded to the partition and needs re-mmap.
static volatile bool g_font_uploaded = false;

// Single-slot SPSC queue for index mutations triggered by serial commands
// (upload via 'W' magic-EPUB, delete via 'R', rename via 'N'). The receiver
// task is the producer; the main loop is the consumer. Single slot is enough
// because the host always waits for the "OK\n" response between operations,
// and the main loop dequeues before processing (so the slot is free quickly).
// If a second op arrives while the slot is still occupied, it is dropped with
// a warning — the file on SD is unchanged, only the index entry is missed;
// recoverable via "Rebuild Book Index" in Settings.
//
// Memory ordering: producer writes path_a/path_b THEN sets g_index_op (commit).
// Consumer reads g_index_op, copies paths to locals, THEN clears g_index_op.
// Volatile is sufficient on ESP32 (32-bit atomic reads/writes) and matches
// the pattern already used by g_cmd_type/g_cmd_path above.
enum class SerialIndexOp : uint8_t { None, Add, Remove, Rename };
static volatile SerialIndexOp g_index_op = SerialIndexOp::None;
static char g_index_path_a[256];  // Add/Remove: the path. Rename: src.
static char g_index_path_b[256];  // Rename: dst.

inline void request_index_op(SerialIndexOp op, const char* a, const char* b = nullptr) {
  if (g_index_op != SerialIndexOp::None) {
    ESP_LOGW(kCmdTag, "index op dropped (slot busy): op=%u path=%s", (unsigned)op, a ? a : "(null)");
    return;  // drop
  }
  if (a) {
    strncpy(g_index_path_a, a, sizeof(g_index_path_a) - 1);
    g_index_path_a[sizeof(g_index_path_a) - 1] = '\0';
  }
  if (b) {
    strncpy(g_index_path_b, b, sizeof(g_index_path_b) - 1);
    g_index_path_b[sizeof(g_index_path_b) - 1] = '\0';
  }
  g_index_op = op;  // commit
}

// Set while a chunked file upload (EPUB/SDFN/SIMG/FONT/CMND-W) is in
// progress. The main loop skips app.update() when this is true to prevent
// display SPI traffic (SPI2_HOST) from contending with SD-card fwrite()
// (also SPI2_HOST). We also silence esp_log during this window so no log
// line can interleave with 0x06 ACK bytes in the shared USB serial TX buffer.
static volatile bool g_upload_in_progress = false;

// Screen name of the top/active screen, updated once per main loop iteration.
// Used by the serial 'Q' debug command.
static char g_top_screen_name[32] = "unknown";

// Call from the main loop. Returns true (and copies into `out`) when a fresh
// LUT has been received since the last call.
// Returns true and sets *type_out if a new LUT is available.
inline bool serial_lut_take(uint8_t* out, uint8_t* type_out = nullptr) {
  if (!g_lut_pending)
    return false;
  memcpy(out, g_lut_buf, kLutSize);
  if (type_out)
    *type_out = g_lut_type;
  g_lut_pending = false;
  return true;
}

// Call from the main loop. Returns the command type and sets *path_out to the
// path string. Returns None (and leaves *path_out unchanged) if nothing pending.
// Clears the pending state before returning.
inline SerialCmdType serial_cmd_take(const char** path_out) {
  SerialCmdType t = g_cmd_type;
  if (t == SerialCmdType::None)
    return SerialCmdType::None;
  if (path_out)
    *path_out = g_cmd_path;
  g_cmd_type = SerialCmdType::None;
  return t;
}

// Read exactly `n` bytes with a timeout. Returns true on success.
static bool serial_read_exact(uint8_t* buf, size_t n, uint32_t timeout_ms) {
  size_t received = 0;
  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
  while (received < n) {
    const TickType_t now = xTaskGetTickCount();
    if ((int32_t)(deadline - now) <= 0)
      return false;
#ifdef QEMU_BUILD
    const int r = uart_read_bytes(UART_NUM_0, buf + received, n - received, deadline - now);
#else
    const int r = usb_serial_jtag_read_bytes(buf + received, n - received, deadline - now);
#endif
    if (r > 0)
      received += r;
  }
  return true;
}

static void serial_write(const char* msg) {
#ifdef QEMU_BUILD
  uart_write_bytes(UART_NUM_0, msg, strlen(msg));
#else
  usb_serial_jtag_write_bytes((const uint8_t*)msg, strlen(msg), pdMS_TO_TICKS(1000));
#endif
}

static void serial_write_raw(const uint8_t* buf, size_t n) {
#ifdef QEMU_BUILD
  uart_write_bytes(UART_NUM_0, buf, n);
#else
  usb_serial_jtag_write_bytes(buf, n, pdMS_TO_TICKS(1000));
#endif
}

// ---------------------------------------------------------------------------
// Handle an incoming LUT frame (after magic has been matched).
// ---------------------------------------------------------------------------
static void handle_lut_frame() {
  uint8_t len_buf[4];
  if (!serial_read_exact(len_buf, 4, 500)) {
    ESP_LOGW(kLutRxTag, "timeout reading length");
    return;
  }
  const uint32_t length =
      (uint32_t)len_buf[0] | ((uint32_t)len_buf[1] << 8) | ((uint32_t)len_buf[2] << 16) | ((uint32_t)len_buf[3] << 24);
  if (length != kLutFrameSize) {
    ESP_LOGW(kLutRxTag, "invalid LUT frame size: %lu (expected %u)", length, kLutFrameSize);
    return;
  }

  uint8_t payload[kLutFrameSize];
  if (!serial_read_exact(payload, length, 2000)) {
    ESP_LOGW(kLutRxTag, "timeout reading payload");
    return;
  }

  uint8_t crc_buf[4];
  if (!serial_read_exact(crc_buf, 4, 500)) {
    ESP_LOGW(kLutRxTag, "timeout reading crc");
    return;
  }
  const uint32_t recv_crc =
      (uint32_t)crc_buf[0] | ((uint32_t)crc_buf[1] << 8) | ((uint32_t)crc_buf[2] << 16) | ((uint32_t)crc_buf[3] << 24);
  const uint32_t calc_crc = esp_rom_crc32_le(0, payload, length);

  if (recv_crc != calc_crc) {
    ESP_LOGE(kLutRxTag, "CRC mismatch: recv=0x%08lX calc=0x%08lX", recv_crc, calc_crc);
    return;
  }

  g_lut_type = payload[0];
  memcpy(g_lut_buf, payload + 1, kLutSize);
  ESP_LOGI(kLutRxTag, "OK: received LUT type %u (%lu bytes, CRC 0x%08lX)", g_lut_type, length, calc_crc);
  g_lut_pending = true;
}

// ---------------------------------------------------------------------------
// Handle an incoming upload to a specific directory (after magic matched).
// ---------------------------------------------------------------------------
static void handle_file_upload(const char* target_dir) {
  // Read filename length (2 bytes LE).
  uint8_t hdr[2];
  if (!serial_read_exact(hdr, 2, 2000)) {
    serial_write("ERR:header\n");
    return;
  }
  uint16_t name_len = hdr[0] | (hdr[1] << 8);
  if (name_len == 0 || name_len > 200) {
    serial_write("ERR:name_len\n");
    return;
  }

  // Read filename.
  char name[204];
  if (!serial_read_exact((uint8_t*)name, name_len, 2000)) {
    serial_write("ERR:name\n");
    return;
  }
  name[name_len] = '\0';

  // Read file size (4 bytes LE).
  uint8_t sz_buf[4];
  if (!serial_read_exact(sz_buf, 4, 2000)) {
    serial_write("ERR:size\n");
    return;
  }
  uint32_t file_size = sz_buf[0] | (sz_buf[1] << 8) | (sz_buf[2] << 16) | (sz_buf[3] << 24);

  // Build path
  char path[256];
  snprintf(path, sizeof(path), "%s/%s", target_dir, name);
  mkdir(target_dir, 0775);

  FILE* f = fopen(path, "wb");
  if (!f) {
    ESP_LOGE(kUpTag, "fopen failed: %s (errno=%d: %s)", path, errno, strerror(errno));
    serial_write("ERR:fopen\n");
    return;
  }

  // Log before READY so the host's readline loop can skip it.
  ESP_LOGI(kUpTag, "receiving '%s' (%lu bytes)", name, (unsigned long)file_size);
  serial_write("READY\n");

  // Silence all ESP_LOG output and signal the main loop to pause UI updates
  // for the duration of the ACK-based transfer. Any log bytes written to the
  // shared USB serial TX buffer while the host expects a 0x06 ACK will be
  // read as garbage (e.g. 'I' from ESP_LOGI) causing "Bad ACK" and abort.
  // Pausing app.update() also prevents display SPI (SPI2_HOST) from
  // contending with SD-card fwrite() (also SPI2_HOST).
  g_upload_in_progress = true;
  esp_log_level_set("*", ESP_LOG_NONE);

  uint32_t crc = 0;
  uint32_t remaining = file_size;
  uint8_t chunk[2048];
  while (remaining > 0) {
    size_t want = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
    if (!serial_read_exact(chunk, want, 30000)) {
      g_upload_in_progress = false;
      esp_log_level_set("*", ESP_LOG_INFO);
      ESP_LOGE(kUpTag, "timeout, %lu bytes remaining", (unsigned long)remaining);
      fclose(f);
      remove(path);
      serial_write("ERR:timeout\n");
      return;
    }
    fwrite(chunk, 1, want, f);
    crc = esp_rom_crc32_le(crc, chunk, want);
    remaining -= want;
    serial_write_raw(&kAck, 1);
  }
  fclose(f);

  // Verify CRC.
  uint8_t crc_buf[4];
  if (!serial_read_exact(crc_buf, 4, 2000)) {
    g_upload_in_progress = false;
    esp_log_level_set("*", ESP_LOG_INFO);
    remove(path);
    serial_write("ERR:crc_missing\n");
    return;
  }
  uint32_t expected = crc_buf[0] | (crc_buf[1] << 8) | (crc_buf[2] << 16) | (crc_buf[3] << 24);
  if (crc != expected) {
    g_upload_in_progress = false;
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGE(kUpTag, "CRC mismatch: got 0x%08lx, expected 0x%08lx", (unsigned long)crc, (unsigned long)expected);
    remove(path);
    serial_write("ERR:crc\n");
    return;
  }

  g_upload_in_progress = false;
  esp_log_level_set("*", ESP_LOG_INFO);
  ESP_LOGI(kUpTag, "saved %s (%lu bytes, CRC OK)", path, (unsigned long)file_size);
  if (strcmp(target_dir, "/sdcard/books") == 0) {
    // EPUB uploads go through request_index_op instead of touching BookIndex
    // directly from this receiver task. The main loop will call index_file()
    // which (via ensure_loaded_) merges with the existing on-disk index.
    request_index_op(SerialIndexOp::Add, path);
  }
  serial_write("OK\n");
}

// ---------------------------------------------------------------------------
// Specific upload handlers
// ---------------------------------------------------------------------------
static void handle_epub_upload() {
  handle_file_upload("/sdcard/books");
}

static void handle_simg_upload() {
  handle_file_upload("/sdcard/sleep");
}

static void handle_sdfnt_upload() {
  handle_file_upload("/sdcard/fonts");
}

// ---------------------------------------------------------------------------
// Handle a FONT upload — write directly to the spiffs partition (raw flash).
// Protocol:
//   [4B] "FONT" magic (already consumed)
//   [4B] file_size (LE)
//   payload in 2KB chunks with ACK flow control (same as EPUB)
//   [4B] CRC32 (LE)
// ---------------------------------------------------------------------------
static void handle_font_upload() {
  // Read file size (4 bytes LE).
  uint8_t sz_buf[4];
  if (!serial_read_exact(sz_buf, 4, 2000)) {
    serial_write("ERR:size\n");
    return;
  }
  uint32_t file_size = sz_buf[0] | (sz_buf[1] << 8) | (sz_buf[2] << 16) | (sz_buf[3] << 24);

  const esp_partition_t* part = FontPartition::find();
  if (!part) {
    serial_write("ERR:no_partition\n");
    return;
  }
  if (kFontPartHeaderSize + file_size > part->size) {
    serial_write("ERR:too_large\n");
    return;
  }

  ESP_LOGI(kUpTag, "receiving font (%lu bytes) → spiffs partition", (unsigned long)file_size);

  // Erase needed sectors BEFORE signaling READY, so the host doesn't
  // start sending while we're busy erasing.
  size_t total = kFontPartHeaderSize + file_size;
  size_t erase_size = (total + 0xFFF) & ~0xFFF;
  if (esp_partition_erase_range(part, 0, erase_size) != ESP_OK) {
    serial_write("ERR:erase\n");
    return;
  }

  // Write the header first (magic + size).
  uint8_t header[kFontPartHeaderSize];
  memcpy(header, kFontMagic, 4);
  memcpy(header + 4, sz_buf, 4);
  if (esp_partition_write(part, 0, header, sizeof(header)) != ESP_OK) {
    serial_write("ERR:write_hdr\n");
    return;
  }

  // Now signal the host to start sending data.
  serial_write("READY\n");

  g_upload_in_progress = true;
  esp_log_level_set("*", ESP_LOG_NONE);

  uint32_t crc = 0;
  uint32_t remaining = file_size;
  uint32_t flash_offset = kFontPartHeaderSize;
  uint8_t chunk[2048];

  while (remaining > 0) {
    size_t want = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
    if (!serial_read_exact(chunk, want, 30000)) {
      g_upload_in_progress = false;
      esp_log_level_set("*", ESP_LOG_INFO);
      ESP_LOGE(kUpTag, "font upload timeout, %lu remaining", (unsigned long)remaining);
      serial_write("ERR:timeout\n");
      return;
    }
    crc = esp_rom_crc32_le(crc, chunk, want);

    if (esp_partition_write(part, flash_offset, chunk, want) != ESP_OK) {
      g_upload_in_progress = false;
      esp_log_level_set("*", ESP_LOG_INFO);
      serial_write("ERR:write\n");
      return;
    }
    flash_offset += want;
    remaining -= want;
    serial_write_raw(&kAck, 1);
  }

  // Verify CRC.
  uint8_t crc_buf[4];
  if (!serial_read_exact(crc_buf, 4, 2000)) {
    g_upload_in_progress = false;
    esp_log_level_set("*", ESP_LOG_INFO);
    serial_write("ERR:crc_missing\n");
    return;
  }
  uint32_t expected = crc_buf[0] | (crc_buf[1] << 8) | (crc_buf[2] << 16) | (crc_buf[3] << 24);
  if (crc != expected) {
    g_upload_in_progress = false;
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGE(kUpTag, "font CRC mismatch: got 0x%08lx expected 0x%08lx", (unsigned long)crc, (unsigned long)expected);
    serial_write("ERR:crc\n");
    return;
  }

  g_upload_in_progress = false;
  esp_log_level_set("*", ESP_LOG_INFO);
  ESP_LOGI(kUpTag, "font saved to partition (%lu bytes, CRC OK)", (unsigned long)file_size);
  g_font_uploaded = true;
  serial_write("OK\n");
}

// ---------------------------------------------------------------------------
// Handle a serial command (after "CMND" magic has been matched).
//
// Sub-commands (1 byte after magic):
//   'A' + 2B path_len + path  → dir listing: "DIR:<p>\n" + "d|name\n" /
//                                "f|name|size|mtime\n" lines + "END\n"
//   'B' + 1B mask             → inject button press(es)
//   'C'                       → clear .mrb cache in /sdcard/.microreader/cache/
//   'D' + 2B path_len + path  → image-decode benchmark
//   'F'                       → invalidate font partition
//   'G'                       → flash erase+write benchmark
//   'I' + 2B path_len + path  → image-size benchmark
//   'K' + 2B path_len + path  → mkdir
//   'L'                       → list books in /sdcard/books/ ("BOOKS:\n" … "END\n")
//   'N' + 2B src_len + src
//       + 2B dst_len + dst    → rename / move
//   'O' + 2B path_len + path  → open book
//   'P'                       → render-page benchmark (current page)
//   'R' + 2B path_len + path  → recursive delete
//   'S'                       → heap status ("STATUS:free=N,largest=M\n")
//   'T' + 2B path_len + path  → read file: "READY\n" + 4B size + [2KB chunks, 0x06 ACK each] + 4B CRC32
//   'W' + 2B path_len + path
//       + 4B size + data
//       + 4B CRC32            → write file (chunked + 0x06 ACKs)
//   'X' + 2B path_len + path  → EPUB conversion benchmark
//   'Y'                       → clear /sdcard/fonts/
//   'Z'                       → clear /sdcard/sleep/
// ---------------------------------------------------------------------------

// Read a 2-byte LE path length followed by the path bytes into g_cmd_path.
// Sends an ERR: response and returns false on any failure.
static bool read_cmd_path(const char* log_label) {
  uint8_t len_buf[2];
  if (!serial_read_exact(len_buf, 2, 1000)) {
    serial_write("ERR:path_len\n");
    return false;
  }
  uint16_t path_len = len_buf[0] | (len_buf[1] << 8);
  if (path_len == 0 || path_len >= sizeof(g_cmd_path)) {
    serial_write("ERR:path_too_long\n");
    return false;
  }
  if (!serial_read_exact((uint8_t*)g_cmd_path, path_len, 2000)) {
    serial_write("ERR:path_read\n");
    return false;
  }
  g_cmd_path[path_len] = '\0';
  ESP_LOGI(kCmdTag, "%s: %s", log_label, g_cmd_path);
  return true;
}

// Recursively delete a file or directory tree. Best-effort: deletes as much as
// possible, does not abort on partial failures.
static void remove_recursive(const char* path) {
  DIR* d = opendir(path);
  if (!d) {
    // Not a directory (or doesn't exist) — try plain remove.
    remove(path);
    return;
  }
  struct dirent* ent;
  char child[300];
  while ((ent = readdir(d)) != nullptr) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
    snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
    if (ent->d_type == DT_DIR) {
      remove_recursive(child);
    } else {
      remove(child);
    }
  }
  closedir(d);
  rmdir(path);
}

static void handle_serial_cmd() {
  uint8_t sub;
  if (!serial_read_exact(&sub, 1, 1000)) {
    ESP_LOGW(kCmdTag, "timeout reading sub-command");
    return;
  }

  switch (sub) {
    case 'B': {
      uint8_t mask;
      if (!serial_read_exact(&mask, 1, 500)) {
        serial_write("ERR:btn_read\n");
        return;
      }
      g_serial_buttons |= mask;
      ESP_LOGI(kCmdTag, "button inject: 0x%02x", mask);
      serial_write("OK\n");
      break;
    }
    case 'O': {
      if (!read_cmd_path("open"))
        return;
      g_cmd_type = SerialCmdType::Open;
      serial_write("OK\n");
      break;
    }
    case 'S': {
      char buf[256];
      snprintf(buf, sizeof(buf), "STATUS:free=%lu,largest=%lu\n", (unsigned long)esp_get_free_heap_size(),
               (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
      serial_write(buf);
      break;
    }
    case 'L': {
      // Response format: "path|title|author|size|mtime\n" per book.
      serial_write("BOOKS:\n");
      const auto& bidx = microreader::BookIndex::instance();
      const auto& entries = bidx.entries();
      const auto& pool = bidx.pool();
      if (!entries.empty()) {
        char lbuf[800];
        for (const auto& e : entries) {
          auto pv = e.path.view(pool);
          auto tv = e.title.view(pool);
          auto av = e.author.view(pool);
          // Get file size and modification time.
          struct stat lst = {};
          char path_c[256];
          snprintf(path_c, sizeof(path_c), "%.*s", (int)pv.size(), pv.data());
          stat(path_c, &lst);
          snprintf(lbuf, sizeof(lbuf), "%.*s|%.*s|%.*s|%lu|%lu\n",
                   (int)pv.size(), pv.data(),
                   (int)tv.size(), tv.data(),
                   (int)av.size(), av.data(),
                   (unsigned long)lst.st_size,
                   (unsigned long)lst.st_mtime);
          serial_write(lbuf);
        }
      } else {
        // Index not yet loaded — fall back to the on-disk file.
        // File format: path|title|author|last_open_order; emit size/mtime from stat.
        FILE* fidx = fopen("/sdcard/.microreader/book_index.dat", "r");
        if (fidx) {
          char line[1024];
          while (fgets(line, sizeof(line), fidx)) {
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
              line[--len] = '\0';
            if (len == 0) continue;
            if (line[0] == '#') continue;
            // Keep only the first 3 fields (drop last_open_order).
            int fields = 0;
            char* path_end = nullptr;
            for (char* p = line; *p; ++p) {
              if (*p == '|') {
                ++fields;
                if (fields == 1) path_end = p;
                if (fields == 3) { *p = '\0'; break; }
              }
            }
            char fpath[256] = {};
            if (path_end) {
              int n = (int)(path_end - line);
              snprintf(fpath, sizeof(fpath), "%.*s", n, line);
            }
            struct stat lst = {};
            stat(fpath, &lst);
            char out[1060];
            snprintf(out, sizeof(out), "%s|%lu|%lu\n", line,
                     (unsigned long)lst.st_size, (unsigned long)lst.st_mtime);
            serial_write(out);
          }
          fclose(fidx);
        }
      }
      serial_write("END\n");
      break;
    }
    case 'C': {
      // Delete all per-book subdirs in /sdcard/.microreader/cache/ and recreate it.
      const char* cache_dir = "/sdcard/.microreader/cache";
      DIR* dir = opendir(cache_dir);
      if (!dir) {
        mkdir(cache_dir, 0775);
        serial_write("CLEARED:0\n");
        break;
      }
      int count = 0;
      struct dirent* ent;
      char subdir[300];
      while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.')
          continue;
        snprintf(subdir, sizeof(subdir), "%s/%s", cache_dir, ent->d_name);
        DIR* sd = opendir(subdir);
        if (sd) {
          struct dirent* sf;
          char fpath[768];
          while ((sf = readdir(sd)) != nullptr) {
            if (sf->d_name[0] == '.')
              continue;
            snprintf(fpath, sizeof(fpath), "%s/%s", subdir, sf->d_name);
            if (remove(fpath) == 0)
              ++count;
          }
          closedir(sd);
        }
        rmdir(subdir);
      }
      closedir(dir);
      rmdir(cache_dir);
      mkdir(cache_dir, 0775);
      char buf[64];
      snprintf(buf, sizeof(buf), "CLEARED:%d\n", count);
      serial_write(buf);
      ESP_LOGI(kCmdTag, "cleared %d cache entries", count);
      break;
    }
    case 'Z': {
      const char* sleep_dir = "/sdcard/sleep";
      DIR* dir = opendir(sleep_dir);
      int count = 0;
      if (dir) {
        struct dirent* ent;
        char fpath[300];
        while ((ent = readdir(dir)) != nullptr) {
          if (ent->d_name[0] == '.')
            continue;
          snprintf(fpath, sizeof(fpath), "%s/%s", sleep_dir, ent->d_name);
          if (remove(fpath) == 0)
            ++count;
        }
        closedir(dir);
      }
      char buf[64];
      snprintf(buf, sizeof(buf), "CLEARED_SLEEP:%d\n", count);
      serial_write(buf);
      ESP_LOGI(kCmdTag, "cleared %d sleep images", count);
      break;
    }
    case 'Y': {
      const char* fonts_dir = "/sdcard/fonts";
      DIR* dir = opendir(fonts_dir);
      int count = 0;
      if (dir) {
        struct dirent* ent;
        char fpath[300];
        while ((ent = readdir(dir)) != nullptr) {
          if (ent->d_name[0] == '.')
            continue;
          snprintf(fpath, sizeof(fpath), "%s/%s", fonts_dir, ent->d_name);
          if (remove(fpath) == 0)
            ++count;
        }
        closedir(dir);
      }
      char buf[64];
      snprintf(buf, sizeof(buf), "CLEARED_SDFONTS:%d\n", count);
      serial_write(buf);
      ESP_LOGI(kCmdTag, "cleared %d SD fonts", count);
      break;
    }
    case 'R': {
      if (!read_cmd_path("rm"))
        return;
      remove_recursive(g_cmd_path);
      struct stat rm_st;
      if (stat(g_cmd_path, &rm_st) != 0) {
        ESP_LOGI(kCmdTag, "removed: %s", g_cmd_path);
        // Schedule index update via the main loop instead of mutating BookIndex
        // here. This avoids a data race with MainMenu::update() (which reads
        // entries_ from the main loop) and ensures ensure_loaded_() runs in
        // the right thread. Non-book paths (e.g. cache files) are ignored.
        if (microreader::BookIndex::is_book_path(g_cmd_path)) {
          request_index_op(SerialIndexOp::Remove, g_cmd_path);
        }
        serial_write("OK\n");
      } else {
        ESP_LOGE(kCmdTag, "remove failed: %s (errno=%d)", g_cmd_path, errno);
        serial_write("ERR:remove_failed\n");
      }
      break;
    }
    case 'X': {
      if (!read_cmd_path("bench"))
        return;
      g_cmd_type = SerialCmdType::Bench;
      serial_write("OK\n");
      break;
    }
    case 'I': {
      if (!read_cmd_path("imgbench"))
        return;
      g_cmd_type = SerialCmdType::ImgBench;
      serial_write("OK\n");
      break;
    }
    case 'D': {
      if (!read_cmd_path("imgdecode"))
        return;
      g_cmd_type = SerialCmdType::ImgDecode;
      serial_write("OK\n");
      break;
    }
    case 'G': {
      // Flash erase+write benchmark — no path argument.
      g_cmd_type = SerialCmdType::FlashBench;
      serial_write("OK\n");
      break;
    }
    case 'F': {
      g_cmd_type = SerialCmdType::InvalidateFont;
      serial_write("FONT_INVALIDATED\n");
      break;
    }
    case 'P': {
      // Render benchmark on the currently open page (no path argument).
      g_cmd_type = SerialCmdType::RenderBench;
      serial_write("OK\n");
      break;
    }
    case 'A': {
      // Directory listing: responds with "DIR:<path>\n", then per-entry lines, then "END\n".
      // File entries: "f|name|size_bytes|mtime_unix\n"
      // Dir entries:  "d|name\n"
      if (!read_cmd_path("ls"))
        return;
      DIR* ldir = opendir(g_cmd_path);
      if (!ldir) {
        ESP_LOGW(kCmdTag, "ls: opendir failed: %s (errno=%d)", g_cmd_path, errno);
        serial_write("ERR:opendir\n");
        return;
      }
      char lline[400];
      snprintf(lline, sizeof(lline), "DIR:%s\n", g_cmd_path);
      serial_write(lline);
      struct dirent* lent;
      while ((lent = readdir(ldir)) != nullptr) {
        if (strcmp(lent->d_name, ".") == 0 || strcmp(lent->d_name, "..") == 0)
          continue;
        char lfull[300];
        snprintf(lfull, sizeof(lfull), "%s/%s", g_cmd_path, lent->d_name);
        struct stat lst;
        bool lis_dir = (lent->d_type == DT_DIR);
        unsigned long lsize = 0;
        long lmtime = 0;
        if (stat(lfull, &lst) == 0) {
          lis_dir = S_ISDIR(lst.st_mode);
          lsize = (unsigned long)lst.st_size;
          lmtime = (long)lst.st_mtime;
        }
        if (lis_dir) {
          snprintf(lline, sizeof(lline), "d|%s\n", lent->d_name);
        } else {
          snprintf(lline, sizeof(lline), "f|%s|%lu|%ld\n", lent->d_name, lsize, lmtime);
        }
        serial_write(lline);
      }
      closedir(ldir);
      serial_write("END\n");
      break;
    }
    case 'W': {
      // Write file to arbitrary /sdcard/ path.
      // Format after 'W': 2-byte LE path_len + path + 4-byte LE size + [2KB chunks + 0x06 ACK] + 4-byte CRC32
      if (!read_cmd_path("write"))
        return;
      if (strncmp(g_cmd_path, "/sdcard/", 8) != 0) {
        serial_write("ERR:invalid_path\n");
        return;
      }
      uint8_t wsz[4];
      if (!serial_read_exact(wsz, 4, 2000)) { serial_write("ERR:size\n"); return; }
      const uint32_t wfsize = wsz[0] | (wsz[1] << 8) | (wsz[2] << 16) | (wsz[3] << 24);
      FILE* wf = fopen(g_cmd_path, "wb");
      if (!wf) {
        ESP_LOGE(kCmdTag, "write: fopen failed: %s (errno=%d)", g_cmd_path, errno);
        serial_write("ERR:fopen\n");
        return;
      }
      ESP_LOGI(kCmdTag, "write: '%s' (%lu bytes)", g_cmd_path, (unsigned long)wfsize);
      serial_write("READY\n");
      g_upload_in_progress = true;
      esp_log_level_set("*", ESP_LOG_NONE);
      uint32_t wcrc = 0, wrem = wfsize;
      uint8_t wchunk[2048];
      while (wrem > 0) {
        const size_t wwant = wrem < sizeof(wchunk) ? wrem : sizeof(wchunk);
        if (!serial_read_exact(wchunk, wwant, 30000)) {
          g_upload_in_progress = false;
          esp_log_level_set("*", ESP_LOG_INFO);
          ESP_LOGE(kCmdTag, "write: timeout, %lu remaining", (unsigned long)wrem);
          fclose(wf); remove(g_cmd_path);
          serial_write("ERR:timeout\n"); return;
        }
        fwrite(wchunk, 1, wwant, wf);
        wcrc = esp_rom_crc32_le(wcrc, wchunk, wwant);
        wrem -= wwant;
        serial_write_raw(&kAck, 1);
      }
      fclose(wf);
      uint8_t wcb[4];
      if (!serial_read_exact(wcb, 4, 2000)) {
        g_upload_in_progress = false;
        esp_log_level_set("*", ESP_LOG_INFO);
        remove(g_cmd_path); serial_write("ERR:crc_missing\n"); return;
      }
      const uint32_t wexp = wcb[0] | (wcb[1] << 8) | (wcb[2] << 16) | (wcb[3] << 24);
      if (wcrc != wexp) {
        g_upload_in_progress = false;
        esp_log_level_set("*", ESP_LOG_INFO);
        ESP_LOGE(kCmdTag, "write: CRC mismatch: got 0x%08lx expected 0x%08lx", (unsigned long)wcrc, (unsigned long)wexp);
        remove(g_cmd_path); serial_write("ERR:crc\n"); return;
      }
      g_upload_in_progress = false;
      esp_log_level_set("*", ESP_LOG_INFO);
      ESP_LOGI(kCmdTag, "write: saved %s (%lu bytes, CRC OK)", g_cmd_path, (unsigned long)wfsize);
      serial_write("OK\n");
      // If the file is a book under /sdcard/, schedule index update via the
      // main loop. The Web Manager uploads EPUBs via this 'W' command rather
      // than the EPUB magic, so without this hook newly uploaded books never
      // appeared in the menu until a manual rebuild.
      if (microreader::BookIndex::is_book_path(g_cmd_path)) {
        request_index_op(SerialIndexOp::Add, g_cmd_path);
      }
      break;
    }
    case 'K': {
      // Make directory: 'K' + 2-byte LE path_len + path
      if (!read_cmd_path("mkdir"))
        return;
      if (mkdir(g_cmd_path, 0775) == 0 || errno == EEXIST) {
        ESP_LOGI(kCmdTag, "mkdir: %s", g_cmd_path);
        serial_write("OK\n");
      } else {
        ESP_LOGE(kCmdTag, "mkdir failed: %s (errno=%d)", g_cmd_path, errno);
        serial_write("ERR:mkdir_failed\n");
      }
      break;
    }
    case 'N': {
      // Rename/move: 'N' + 2-byte LE src_len + src + 2-byte LE dst_len + dst
      if (!read_cmd_path("rename_src"))
        return;
      char nsrc[256];
      strncpy(nsrc, g_cmd_path, sizeof(nsrc) - 1);
      nsrc[sizeof(nsrc) - 1] = '\0';
      if (!read_cmd_path("rename_dst"))
        return;
      if (rename(nsrc, g_cmd_path) == 0) {
        ESP_LOGI(kCmdTag, "renamed: %s -> %s", nsrc, g_cmd_path);
        // Update the book index based on what changed. Three cases:
        //   src book + dst book  → Rename (preserves metadata + last_open_order)
        //   src book + dst non   → Remove (file is no longer a book)
        //   src non  + dst book  → Add    (file became a book, e.g. .txt → .epub)
        const bool src_is_book = microreader::BookIndex::is_book_path(nsrc);
        const bool dst_is_book = microreader::BookIndex::is_book_path(g_cmd_path);
        if (src_is_book && dst_is_book) {
          request_index_op(SerialIndexOp::Rename, nsrc, g_cmd_path);
        } else if (src_is_book) {
          request_index_op(SerialIndexOp::Remove, nsrc);
        } else if (dst_is_book) {
          request_index_op(SerialIndexOp::Add, g_cmd_path);
        }
        serial_write("OK\n");
      } else {
        ESP_LOGE(kCmdTag, "rename failed: %s -> %s (errno=%d)", nsrc, g_cmd_path, errno);
        serial_write("ERR:rename_failed\n");
      }
      break;
    }
    case 'T': {
      // Read file: host receives "READY\n" + 4B size LE + [2KB chunks, each
      // ACKed with 0x06 from the host] + 4B CRC32 LE.
      //
      // The ACK-per-chunk flow control mirrors the upload protocol ('W') and
      // prevents USB-CDC TX buffer overruns that cause data corruption or
      // transfer aborts on larger files. Without pacing, the device writes
      // faster than the host can drain the shared USB serial TX buffer, leading
      // to dropped bytes and CRC mismatches on the receiving end.
      if (!read_cmd_path("read"))
        return;
      FILE* tf = fopen(g_cmd_path, "rb");
      if (!tf) {
        ESP_LOGE(kCmdTag, "read: fopen failed: %s (errno=%d)", g_cmd_path, errno);
        serial_write("ERR:fopen\n");
        return;
      }
      struct stat tst;
      fstat(fileno(tf), &tst);
      const uint32_t tfsize = (uint32_t)tst.st_size;
      serial_write("READY\n");
      // Silence ESP_LOG and pause UI updates for the duration of the binary
      // transfer — same as upload handlers. Any log byte interleaved with chunk
      // data will be read by the host as a misaligned data byte, corrupting the
      // file and causing a CRC mismatch.
      g_upload_in_progress = true;
      esp_log_level_set("*", ESP_LOG_NONE);
      uint8_t tszb[4] = {(uint8_t)tfsize, (uint8_t)(tfsize>>8), (uint8_t)(tfsize>>16), (uint8_t)(tfsize>>24)};
      serial_write_raw(tszb, 4);
      uint32_t tcrc = 0;
      uint8_t tchunk[2048];
      size_t tn;
      bool terror = false;
      while ((tn = fread(tchunk, 1, sizeof(tchunk), tf)) > 0) {
        serial_write_raw(tchunk, tn);
        tcrc = esp_rom_crc32_le(tcrc, tchunk, tn);
        // Wait for the host's 0x06 ACK before sending the next chunk.
        uint8_t tack;
        if (!serial_read_exact(&tack, 1, 30000)) {
          ESP_LOGE(kCmdTag, "read: ACK timeout after chunk (%s)", g_cmd_path);
          terror = true;
          break;
        }
        if (tack != kAck) {
          ESP_LOGE(kCmdTag, "read: bad ACK 0x%02x (%s)", tack, g_cmd_path);
          terror = true;
          break;
        }
      }
      fclose(tf);
      g_upload_in_progress = false;
      esp_log_level_set("*", ESP_LOG_INFO);
      if (terror)
        break;
      uint8_t tcrb[4] = {(uint8_t)tcrc, (uint8_t)(tcrc>>8), (uint8_t)(tcrc>>16), (uint8_t)(tcrc>>24)};
      serial_write_raw(tcrb, 4);
      ESP_LOGI(kCmdTag, "read: sent %s (%lu bytes, CRC 0x%08lx)", g_cmd_path, (unsigned long)tfsize, (unsigned long)tcrc);
      break;
    }
    default:
      ESP_LOGW(kCmdTag, "unknown sub-command: 0x%02x", sub);
      serial_write("ERR:unknown_cmd\n");
      break;
    case 'Q': {
      // Debug: query active screen + index state.
      char buf[128];
      snprintf(buf, sizeof(buf), "SCREEN:%s|ENTRIES:%zu|GEN:%llu|OP:%u\n",
               g_top_screen_name,
               microreader::BookIndex::instance().entries().size(),
               (unsigned long long)microreader::BookIndex::instance().generation(),
               (unsigned)g_index_op);
      serial_write(buf);
      break;
    }
  }
}
// ---------------------------------------------------------------------------
static void serial_receiver_task(void* /*arg*/) {
  uint8_t lut_pos = 0;   // progress matching kLutMagic
  uint8_t epub_pos = 0;  // progress matching kEpubMagic
  uint8_t simg_pos = 0;  // progress matching kSimgMagic
  uint8_t sdfn_pos = 0;  // progress matching kSdFntMagic
  uint8_t cmd_pos = 0;   // progress matching kCmdMagic
  uint8_t font_pos = 0;  // progress matching kFontMagic

  ESP_LOGI(kLutRxTag, "receiver ready (LUT + EPUB + SIMG + SDFN + CMD + FONT)");

  while (true) {
    uint8_t byte;
#ifdef QEMU_BUILD
    if (uart_read_bytes(UART_NUM_0, &byte, 1, pdMS_TO_TICKS(50)) != 1)
#else
    if (usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(50)) != 1)
#endif
      continue;

    // Match LUT magic.
    if (byte == kLutMagic[lut_pos]) {
      if (++lut_pos == 4) {
        lut_pos = 0;
        epub_pos = 0;
        simg_pos = 0;
        sdfn_pos = 0;
        cmd_pos = 0;
        font_pos = 0;
        handle_lut_frame();
        continue;
      }
    } else {
      lut_pos = (byte == kLutMagic[0]) ? 1 : 0;
    }

    // Match EPUB magic.
    if (byte == kEpubMagic[epub_pos]) {
      if (++epub_pos == 4) {
        lut_pos = 0;
        epub_pos = 0;
        simg_pos = 0;
        sdfn_pos = 0;
        cmd_pos = 0;
        font_pos = 0;
        handle_epub_upload();
        continue;
      }
    } else {
      epub_pos = (byte == kEpubMagic[0]) ? 1 : 0;
    }

    // Match SIMG magic.
    if (byte == kSimgMagic[simg_pos]) {
      if (++simg_pos == 4) {
        lut_pos = 0;
        epub_pos = 0;
        simg_pos = 0;
        sdfn_pos = 0;
        cmd_pos = 0;
        font_pos = 0;
        handle_simg_upload();
        continue;
      }
    } else {
      simg_pos = (byte == kSimgMagic[0]) ? 1 : 0;
    }

    // Match SDFN magic.
    if (byte == kSdFntMagic[sdfn_pos]) {
      if (++sdfn_pos == 4) {
        lut_pos = 0;
        epub_pos = 0;
        simg_pos = 0;
        sdfn_pos = 0;
        cmd_pos = 0;
        font_pos = 0;
        handle_sdfnt_upload();
        continue;
      }
    } else {
      sdfn_pos = (byte == kSdFntMagic[0]) ? 1 : 0;
    }

    // Match CMND magic.
    if (byte == kCmdMagic[cmd_pos]) {
      if (++cmd_pos == 4) {
        lut_pos = 0;
        epub_pos = 0;
        simg_pos = 0;
        sdfn_pos = 0;
        cmd_pos = 0;
        font_pos = 0;
        handle_serial_cmd();
        continue;
      }
    } else {
      cmd_pos = (byte == kCmdMagic[0]) ? 1 : 0;
    }

    // Match FONT magic.
    if (byte == kFontMagic[font_pos]) {
      if (++font_pos == 4) {
        lut_pos = 0;
        epub_pos = 0;
        simg_pos = 0;
        sdfn_pos = 0;
        cmd_pos = 0;
        font_pos = 0;
        handle_font_upload();
        continue;
      }
    } else {
      font_pos = (byte == kFontMagic[0]) ? 1 : 0;
    }
  }
}

// Call once from app_main before the main loop.
inline void serial_start() {
#ifdef QEMU_BUILD
  // QEMU simulates UART0; route the binary protocol over it.
  const uart_config_t uart_cfg = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  uart_param_config(UART_NUM_0, &uart_cfg);
  uart_driver_install(UART_NUM_0, 4096, 0, 0, nullptr, 0);
  uart_vfs_dev_use_driver(0);
#else
  usb_serial_jtag_driver_config_t cfg = {
      .tx_buffer_size = 1024,
      .rx_buffer_size = 4096,
  };
  usb_serial_jtag_driver_install(&cfg);
  usb_serial_jtag_vfs_register();
#endif
  xTaskCreate(serial_receiver_task, "serial_rx", 8192, nullptr, 3, nullptr);
}
