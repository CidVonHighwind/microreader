#pragma once

// Wi-Fi file transfer: the HTTP API and web page, independent of the
// network stack. The ESP32 adapter (platforms/esp32/wifi_transfer.h) feeds
// requests from esp_http_server; tests feed them directly.
//
// Paths in the API are relative to the storage root ("/" = the SD card),
// always start with "/", and may not contain "..".
//
//   GET  /                        the Sami Reader page
//   GET  /api/info                {"name","version","total","free"}
//   GET  /api/list?path=/books    [{"name","dir","size","mtime"}, ...]
//   GET  /api/file?path=/a.epub   download
//   POST /api/upload?path=/books/a.epub   body = file bytes (overwrites)
//   POST /api/mkdir?path=/new
//   POST /api/delete?path=/old    files, or folders with everything inside
//   POST /api/rename?from=/a&to=/b
//
// Other GET paths redirect to "/" (so phones' captive-portal checks open the
// page while connected to the reader's hotspot).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace microreader {
namespace net {

struct HttpRequest {
  std::string method;  // "GET", "POST", ...
  std::string path;    // without the query string
  std::string query;   // raw, still percent-encoded (no leading '?')
  int64_t content_length = 0;
  // Reads up to `size` body bytes; returns the count, 0 at the end, < 0 on error.
  std::function<int(char* buf, size_t size)> read_body;
};

class HttpResponse {
 public:
  virtual ~HttpResponse() = default;
  // Call before the first body byte. `status` like "200 OK".
  virtual void begin(const char* status, const char* content_type) = 0;
  virtual void header(const char* name, const char* value) = 0;
  // Send a body chunk. Returns false if the client went away.
  virtual bool write(const char* data, size_t size) = 0;
  // End of the body.
  virtual void finish() = 0;
};

// Callbacks into the rest of the firmware. All optional.
struct FileServerHooks {
  // A file was created/overwritten, removed, or renamed (full device paths).
  // Used to keep the book index in sync. When a folder is deleted or renamed,
  // file_removed / file_renamed are called once for each EPUB inside it.
  std::function<void(const std::string& path)> file_added;
  std::function<void(const std::string& path)> file_removed;
  std::function<void(const std::string& from, const std::string& to)> file_renamed;
  // Something happened worth showing on the reader's screen ("Received x.epub").
  std::function<void(const std::string& text)> activity;
  // True while a long write runs (the firmware pauses display updates, which
  // share the SPI bus with the SD card).
  std::function<void(bool busy)> busy;
  // Storage size in bytes; return false if unknown.
  std::function<bool(uint64_t& total, uint64_t& free)> storage;
};

class FileServer {
 public:
  explicit FileServer(std::string root = "/sdcard", FileServerHooks hooks = {});

  void handle(const HttpRequest& req, HttpResponse& resp);

  const std::string& root() const {
    return root_;
  }

  // ── Helpers (public for tests) ──
  static std::string url_decode(std::string_view s);
  // Value of `key` in a query string, decoded. False if absent.
  static bool query_param(std::string_view query, std::string_view key, std::string& out);
  static std::string json_escape(std::string_view s);
  // "/books/a.epub" → true. Rejects "", missing leading '/', "..", "\\" and control characters.
  static bool safe_relative_path(std::string_view p);
  // True if any part of the path starts with '.' ("/.microreader/x", "/a/.b").
  // The book list ignores such files, like the original firmware's scan does.
  static bool has_hidden_part(std::string_view p);

  static constexpr size_t kChunk = 4096;

 private:
  std::string full_(const std::string& rel) const;
  void send_json_(HttpResponse& resp, const char* status, const std::string& json);
  void send_error_(HttpResponse& resp, const char* status, const char* message);
  void serve_page_(HttpResponse& resp);
  void api_info_(HttpResponse& resp);
  void api_list_(const std::string& rel, HttpResponse& resp);
  void api_file_(const std::string& rel, HttpResponse& resp);
  void api_upload_(const std::string& rel, const HttpRequest& req, HttpResponse& resp);
  void api_mkdir_(const std::string& rel, HttpResponse& resp);
  void api_delete_(const std::string& rel, HttpResponse& resp);
  void api_rename_(const std::string& from, const std::string& to, HttpResponse& resp);
  void note_(const std::string& text);

  std::string root_;
  FileServerHooks hooks_;
};

}  // namespace net
}  // namespace microreader
