#include "FileServer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../content/BookIndex.h"
#include "../version.h"
#include "WebPage.h"

#ifdef ESP_PLATFORM
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <chrono>
#include <filesystem>
#endif

namespace microreader {
namespace net {

namespace {

// ── Minimal filesystem layer (dirent on the device, std::filesystem elsewhere) ──

struct Entry {
  std::string name;
  bool dir = false;
  uint64_t size = 0;
  int64_t mtime = 0;
};

#ifdef ESP_PLATFORM

bool stat_path(const std::string& p, Entry& e) {
  struct stat st;
  if (stat(p.c_str(), &st) != 0)
    return false;
  e.dir = S_ISDIR(st.st_mode);
  e.size = e.dir ? 0 : static_cast<uint64_t>(st.st_size);
  e.mtime = static_cast<int64_t>(st.st_mtime);
  return true;
}

template <typename F>
bool list_dir(const std::string& p, F&& fn) {
  DIR* d = opendir(p.c_str());
  if (!d)
    return false;
  struct dirent* ent;
  while ((ent = readdir(d)) != nullptr) {
    if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0)
      continue;
    Entry e;
    e.name = ent->d_name;
    if (!stat_path(p + "/" + e.name, e))
      e.dir = ent->d_type == DT_DIR;
    fn(e);
  }
  closedir(d);
  return true;
}

bool make_dir(const std::string& p) {
  return mkdir(p.c_str(), 0775) == 0;
}

void remove_tree(const std::string& p) {
  Entry e;
  if (!stat_path(p, e))
    return;
  if (e.dir) {
    std::vector<std::string> names;
    list_dir(p, [&](const Entry& c) { names.push_back(c.name); });
    for (const auto& n : names)
      remove_tree(p + "/" + n);
    rmdir(p.c_str());
  } else {
    std::remove(p.c_str());
  }
}

bool rename_path(const std::string& a, const std::string& b) {
  return std::rename(a.c_str(), b.c_str()) == 0;
}

#else  // desktop / tests

namespace fs = std::filesystem;

int64_t to_unix(fs::file_time_type t) {
  using namespace std::chrono;
  const auto sys = time_point_cast<system_clock::duration>(t - fs::file_time_type::clock::now() + system_clock::now());
  return static_cast<int64_t>(system_clock::to_time_t(sys));
}

bool stat_path(const std::string& p, Entry& e) {
  std::error_code ec;
  const auto st = fs::status(p, ec);
  if (ec || !fs::exists(st))
    return false;
  e.dir = fs::is_directory(st);
  e.size = e.dir ? 0 : static_cast<uint64_t>(fs::file_size(p, ec));
  const auto t = fs::last_write_time(p, ec);
  e.mtime = ec ? 0 : to_unix(t);
  return true;
}

template <typename F>
bool list_dir(const std::string& p, F&& fn) {
  std::error_code ec;
  fs::directory_iterator it(p, ec), end;
  if (ec)
    return false;
  for (; it != end; it.increment(ec)) {
    if (ec)
      break;
    Entry e;
    e.name = it->path().filename().string();
    stat_path(it->path().string(), e);
    fn(e);
  }
  return true;
}

bool make_dir(const std::string& p) {
  std::error_code ec;
  return fs::create_directory(p, ec) && !ec;
}

void remove_tree(const std::string& p) {
  std::error_code ec;
  fs::remove_all(p, ec);
}

bool rename_path(const std::string& a, const std::string& b) {
  std::error_code ec;
  fs::rename(a, b, ec);
  return !ec;
}

#endif

// Every EPUB under `dir` (full paths), for keeping the book index in sync
// when a whole folder is deleted or renamed. Capped at the index size.
void collect_books(const std::string& dir, std::vector<std::string>& out, int depth = 0) {
  if (depth > 12 || out.size() >= static_cast<size_t>(MAX_BOOKS))
    return;
  std::vector<std::string> subdirs;
  list_dir(dir, [&](const Entry& e) {
    const std::string p = dir + "/" + e.name;
    if (e.dir)
      subdirs.push_back(p);
    else if (out.size() < static_cast<size_t>(MAX_BOOKS) && BookIndex::is_book_path(p.c_str()))
      out.push_back(p);
  });
  for (const auto& d : subdirs)
    collect_books(d, out, depth + 1);
}

std::string base_name(const std::string& rel) {
  const size_t s = rel.rfind('/');
  return s == std::string::npos ? rel : rel.substr(s + 1);
}

std::string parent_of(const std::string& rel) {
  const size_t s = rel.rfind('/');
  return (s == std::string::npos || s == 0) ? std::string("/") : rel.substr(0, s);
}

std::string human_size(uint64_t bytes) {
  // Integer maths only: the firmware's newlib-nano printf has no %f.
  char b[32];
  if (bytes >= 1024u * 1024u) {
    const uint64_t tenths = bytes * 10 / (1024u * 1024u);
    std::snprintf(b, sizeof(b), "%u.%u MB", static_cast<unsigned>(tenths / 10), static_cast<unsigned>(tenths % 10));
  } else {
    std::snprintf(b, sizeof(b), "%u KB", static_cast<unsigned>((bytes + 1023) / 1024));
  }
  return b;
}

}  // namespace

FileServer::FileServer(std::string root, FileServerHooks hooks) : root_(std::move(root)), hooks_(std::move(hooks)) {
  while (root_.size() > 1 && root_.back() == '/')
    root_.pop_back();
}

// ── Helpers ─────────────────────────────────────────────────────────────────

std::string FileServer::url_decode(std::string_view s) {
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '+') {
      out.push_back(' ');
    } else if (c == '%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
      out.push_back(static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2])));
      i += 2;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

bool FileServer::query_param(std::string_view query, std::string_view key, std::string& out) {
  size_t pos = 0;
  while (pos <= query.size()) {
    size_t amp = query.find('&', pos);
    if (amp == std::string_view::npos)
      amp = query.size();
    const std::string_view part = query.substr(pos, amp - pos);
    const size_t eq = part.find('=');
    const std::string_view k = part.substr(0, eq);
    if (k == key) {
      out = eq == std::string_view::npos ? std::string() : url_decode(part.substr(eq + 1));
      return true;
    }
    pos = amp + 1;
  }
  return false;
}

std::string FileServer::json_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (const char c : s) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char b[8];
          std::snprintf(b, sizeof(b), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
          out += b;
        } else {
          out.push_back(c);
        }
    }
  }
  return out;
}

bool FileServer::safe_relative_path(std::string_view p) {
  if (p.empty() || p[0] != '/' || p.size() > 400)
    return false;
  for (const char c : p)
    if (c == '\\' || static_cast<unsigned char>(c) < 0x20)
      return false;
  // Reject "." and ".." path segments.
  size_t pos = 0;
  while (pos < p.size()) {
    size_t next = p.find('/', pos + 1);
    if (next == std::string_view::npos)
      next = p.size();
    const std::string_view seg = p.substr(pos + 1, next - pos - 1);
    if (seg == ".." || seg == ".")
      return false;
    pos = next;
  }
  return true;
}

bool FileServer::has_hidden_part(std::string_view p) {
  for (size_t i = 0; i < p.size(); ++i)
    if (p[i] == '.' && (i == 0 || p[i - 1] == '/'))
      return true;
  return false;
}

std::string FileServer::full_(const std::string& rel) const {
  if (rel == "/")
    return root_;
  std::string r = rel;
  while (r.size() > 1 && r.back() == '/')
    r.pop_back();
  return root_ + r;
}

void FileServer::note_(const std::string& text) {
  if (hooks_.activity)
    hooks_.activity(text);
}

void FileServer::send_json_(HttpResponse& resp, const char* status, const std::string& json) {
  resp.begin(status, "application/json");
  resp.header("Cache-Control", "no-store");
  resp.write(json.data(), json.size());
  resp.finish();
}

void FileServer::send_error_(HttpResponse& resp, const char* status, const char* message) {
  send_json_(resp, status, std::string("{\"error\":\"") + json_escape(message) + "\"}");
}

// ── Dispatch ────────────────────────────────────────────────────────────────

void FileServer::handle(const HttpRequest& req, HttpResponse& resp) {
  const bool get = req.method == "GET" || req.method == "HEAD";
  const bool post = req.method == "POST";

  if (req.path.rfind("/api/", 0) != 0) {
    if (!get) {
      send_error_(resp, "405 Method Not Allowed", "Not allowed");
      return;
    }
    if (req.path == "/" || req.path == "/index.html") {
      serve_page_(resp);
      return;
    }
    // Anything else (including captive-portal checks) goes to the page.
    resp.begin("302 Found", "text/plain");
    resp.header("Location", "/");
    resp.finish();
    return;
  }

  const std::string api = req.path.substr(5);
  std::string path;
  const bool has_path = query_param(req.query, "path", path);

  if (api == "info" && get) {
    api_info_(resp);
    return;
  }
  if (api == "rename" && post) {
    std::string from, to;
    if (!query_param(req.query, "from", from) || !query_param(req.query, "to", to) || !safe_relative_path(from) ||
        !safe_relative_path(to)) {
      send_error_(resp, "400 Bad Request", "Invalid path");
      return;
    }
    api_rename_(from, to, resp);
    return;
  }

  const bool known = (api == "list" || api == "file") ? get : (api == "upload" || api == "mkdir" || api == "delete") && post;
  if (!known) {
    send_error_(resp, "404 Not Found", "Unknown request");
    return;
  }
  if (!has_path || !safe_relative_path(path)) {
    send_error_(resp, "400 Bad Request", "Invalid path");
    return;
  }
  if (api == "list")
    api_list_(path, resp);
  else if (api == "file")
    api_file_(path, resp);
  else if (api == "upload")
    api_upload_(path, req, resp);
  else if (api == "mkdir")
    api_mkdir_(path, resp);
  else
    api_delete_(path, resp);
}

// ── Handlers ────────────────────────────────────────────────────────────────

void FileServer::serve_page_(HttpResponse& resp) {
  resp.begin("200 OK", "text/html; charset=utf-8");
  resp.header("Cache-Control", "no-cache");
  const char* p = reinterpret_cast<const char*>(kWebPage);
  size_t left = kWebPageSize;
  while (left > 0) {
    const size_t n = std::min(left, kChunk);
    if (!resp.write(p, n))
      return;
    p += n;
    left -= n;
  }
  resp.finish();
}

void FileServer::api_info_(HttpResponse& resp) {
  uint64_t total = 0, free_b = 0;
  const bool known = hooks_.storage && hooks_.storage(total, free_b);
  char buf[192];
  if (known)
    std::snprintf(buf, sizeof(buf), "{\"name\":\"Sami Reader\",\"version\":\"%s\",\"total\":%lu,\"free\":%lu}",
                  MICROREADER_VERSION, static_cast<unsigned long>(total / 1024), static_cast<unsigned long>(free_b / 1024));
  else
    std::snprintf(buf, sizeof(buf), "{\"name\":\"Sami Reader\",\"version\":\"%s\"}", MICROREADER_VERSION);
  // total/free are in KB: 32-bit is plenty and the device's printf has no 64-bit formats.
  send_json_(resp, "200 OK", buf);
}

void FileServer::api_list_(const std::string& rel, HttpResponse& resp) {
  const std::string dir = full_(rel);
  Entry self;
  if (!stat_path(dir, self) || !self.dir) {
    send_error_(resp, "404 Not Found", "Folder not found");
    return;
  }
  resp.begin("200 OK", "application/json");
  resp.header("Cache-Control", "no-store");
  // Stream the listing in chunks: big folders shouldn't need one big string.
  std::string chunk = "[";
  bool first = true;
  bool alive = true;
  list_dir(dir, [&](const Entry& e) {
    if (!alive)
      return;
    char nums[96];
    std::snprintf(nums, sizeof(nums), "\",\"dir\":%s,\"size\":%lu,\"mtime\":%ld}", e.dir ? "true" : "false",
                  static_cast<unsigned long>(e.size), static_cast<long>(e.mtime));
    if (!first)
      chunk += ',';
    first = false;
    chunk += "{\"name\":\"";
    chunk += json_escape(e.name);
    chunk += nums;
    if (chunk.size() > 1024) {
      alive = resp.write(chunk.data(), chunk.size());
      chunk.clear();
    }
  });
  chunk += ']';
  if (alive)
    resp.write(chunk.data(), chunk.size());
  resp.finish();
}

void FileServer::api_file_(const std::string& rel, HttpResponse& resp) {
  const std::string path = full_(rel);
  Entry e;
  if (!stat_path(path, e) || e.dir) {
    send_error_(resp, "404 Not Found", "File not found");
    return;
  }
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    send_error_(resp, "500 Internal Server Error", "Can't open the file");
    return;
  }
  const std::string name = base_name(rel);
  std::string disp = "attachment; filename=\"";
  for (const char c : name)
    disp.push_back(c == '"' ? '\'' : c);
  disp += '"';
  resp.begin("200 OK", "application/octet-stream");
  resp.header("Content-Disposition", disp.c_str());
  std::vector<char> buf(kChunk);
  size_t n;
  while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0)
    if (!resp.write(buf.data(), n))
      break;
  std::fclose(f);
  resp.finish();
  note_("Sent " + name);
}

void FileServer::api_upload_(const std::string& rel, const HttpRequest& req, HttpResponse& resp) {
  if (rel == "/" || rel.back() == '/') {
    send_error_(resp, "400 Bad Request", "Missing file name");
    return;
  }
  const std::string path = full_(rel);
  const std::string name = base_name(rel);
  Entry parent, existing;
  if (!stat_path(full_(parent_of(rel)), parent) || !parent.dir) {
    send_error_(resp, "404 Not Found", "Folder not found");
    return;
  }
  if (stat_path(path, existing) && existing.dir) {
    send_error_(resp, "409 Conflict", "A folder with that name exists");
    return;
  }

  const std::string part = path + ".part";
  FILE* f = std::fopen(part.c_str(), "wb");
  if (!f) {
    send_error_(resp, "500 Internal Server Error", "Can't create the file");
    return;
  }
  if (hooks_.busy)
    hooks_.busy(true);
  std::vector<char> buf(kChunk);
  int64_t received = 0;
  bool ok = true;
  while (received < req.content_length) {
    const size_t want = static_cast<size_t>(std::min<int64_t>(static_cast<int64_t>(buf.size()), req.content_length - received));
    const int n = req.read_body ? req.read_body(buf.data(), want) : -1;
    if (n <= 0) {
      ok = false;  // client gone or timed out
      break;
    }
    if (std::fwrite(buf.data(), 1, static_cast<size_t>(n), f) != static_cast<size_t>(n)) {
      ok = false;  // card full?
      break;
    }
    received += n;
  }
  if (std::fclose(f) != 0)
    ok = false;
  if (ok) {
    if (stat_path(path, existing))
      std::remove(path.c_str());  // FAT rename can't overwrite
    ok = rename_path(part, path);
  }
  if (!ok)
    std::remove(part.c_str());
  if (hooks_.busy)
    hooks_.busy(false);

  if (!ok) {
    send_error_(resp, "500 Internal Server Error", "Upload failed (card full or connection lost?)");
    return;
  }
  if (hooks_.file_added)
    hooks_.file_added(path);
  note_("Received " + name + " (" + human_size(static_cast<uint64_t>(received)) + ")");
  send_json_(resp, "200 OK", "{\"ok\":true}");
}

void FileServer::api_mkdir_(const std::string& rel, HttpResponse& resp) {
  Entry e;
  if (rel == "/" || stat_path(full_(rel), e)) {
    send_error_(resp, "409 Conflict", "That name is already taken");
    return;
  }
  if (!make_dir(full_(rel))) {
    send_error_(resp, "500 Internal Server Error", "Can't create the folder");
    return;
  }
  note_("Created folder " + base_name(rel));
  send_json_(resp, "200 OK", "{\"ok\":true}");
}

void FileServer::api_delete_(const std::string& rel, HttpResponse& resp) {
  const std::string path = full_(rel);
  Entry e;
  if (rel == "/" || !stat_path(path, e)) {
    send_error_(resp, "404 Not Found", "Not found");
    return;
  }
  // Books inside a folder leave the book list too.
  std::vector<std::string> books;
  if (e.dir && hooks_.file_removed)
    collect_books(path, books);
  if (hooks_.busy)
    hooks_.busy(true);
  remove_tree(path);
  if (hooks_.busy)
    hooks_.busy(false);
  if (hooks_.file_removed) {
    Entry gone;
    if (!e.dir)
      books.push_back(path);
    for (const auto& b : books)
      if (!stat_path(b, gone))
        hooks_.file_removed(b);
  }
  if (stat_path(path, e)) {
    send_error_(resp, "500 Internal Server Error", "Couldn't delete everything");
    return;
  }
  note_("Deleted " + base_name(rel));
  send_json_(resp, "200 OK", "{\"ok\":true}");
}

void FileServer::api_rename_(const std::string& from, const std::string& to, HttpResponse& resp) {
  Entry e, taken;
  if (from == "/" || to == "/" || !stat_path(full_(from), e)) {
    send_error_(resp, "404 Not Found", "Not found");
    return;
  }
  if (stat_path(full_(to), taken)) {
    send_error_(resp, "409 Conflict", "That name is already taken");
    return;
  }
  const std::string a = full_(from), b = full_(to);
  if (!rename_path(a, b)) {
    send_error_(resp, "500 Internal Server Error", "Can't rename");
    return;
  }
  if (hooks_.file_renamed) {
    if (!e.dir) {
      hooks_.file_renamed(a, b);
    } else {
      // A folder: each book inside moved with it.
      std::vector<std::string> books;
      collect_books(b, books);
      for (const auto& nb : books)
        hooks_.file_renamed(a + nb.substr(b.size()), nb);
    }
  }
  note_("Renamed " + base_name(from) + " to " + base_name(to));
  send_json_(resp, "200 OK", "{\"ok\":true}");
}

}  // namespace net
}  // namespace microreader
