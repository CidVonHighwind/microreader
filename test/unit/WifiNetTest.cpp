// Wi-Fi File Transfer, network-independent parts: the HTTP API (FileServer),
// the DNS/mDNS message builders, remembered networks and the embedded page.

#include <gtest/gtest.h>
#include <miniz.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "microreader/net/Dns.h"
#include "microreader/net/FileServer.h"
#include "microreader/net/SavedWifi.h"
#include "microreader/net/WebPage.h"

namespace fs = std::filesystem;
using namespace microreader;
using namespace microreader::net;

namespace {

// Records what the server sends.
class FakeResponse final : public HttpResponse {
 public:
  void begin(const char* s, const char* type) override {
    ++begins;
    status = s;
    content_type = type;
  }
  void header(const char* name, const char* value) override {
    headers[name] = value;
  }
  bool write(const char* data, size_t size) override {
    body.append(data, size);
    ++writes;
    return !client_gone;
  }
  void finish() override {
    ++finishes;
  }
  int code() const {
    return std::atoi(status.c_str());
  }

  std::string status, content_type, body;
  std::map<std::string, std::string> headers;
  int begins = 0, writes = 0, finishes = 0;
  bool client_gone = false;
};

std::string read_file(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
void write_file(const fs::path& p, const std::string& data) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  f << data;
}
int count_of(const std::string& s, const std::string& what) {
  int n = 0;
  for (size_t pos = s.find(what); pos != std::string::npos; pos = s.find(what, pos + what.size()))
    ++n;
  return n;
}

class FileServerTest : public ::testing::Test {
 protected:
  fs::path root_;
  std::vector<std::string> added_, removed_, activity_;
  std::vector<std::pair<std::string, std::string>> renamed_;
  std::vector<bool> busy_;
  std::unique_ptr<FileServer> server_;

  void SetUp() override {
    root_ = fs::temp_directory_path() /
            ("fileserver_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    fs::remove_all(root_);
    fs::create_directories(root_ / "card");
    FileServerHooks h;
    h.file_added = [this](const std::string& p) { added_.push_back(p); };
    h.file_removed = [this](const std::string& p) { removed_.push_back(p); };
    h.file_renamed = [this](const std::string& a, const std::string& b) { renamed_.emplace_back(a, b); };
    h.activity = [this](const std::string& t) { activity_.push_back(t); };
    h.busy = [this](bool b) { busy_.push_back(b); };
    h.storage = [](uint64_t& total, uint64_t& free) {
      total = 32ull * 1024 * 1024 * 1024;  // 32 GB: more than 32 bits of bytes
      free = 20ull * 1024 * 1024 * 1024;
      return true;
    };
    server_ = std::make_unique<FileServer>(card().string() + "/", h);
  }
  void TearDown() override {
    fs::remove_all(root_);
  }
  fs::path card() const {
    return root_ / "card";
  }
  std::string full(const std::string& rel) const {
    return card().string() + rel;
  }

  FakeResponse get(const std::string& path, const std::string& query = "") {
    HttpRequest r;
    r.method = "GET";
    r.path = path;
    r.query = query;
    FakeResponse resp;
    server_->handle(r, resp);
    return resp;
  }
  FakeResponse post(const std::string& path, const std::string& query, const std::string& body = "",
                    size_t chunk = 1000, int fail_after = -1) {
    HttpRequest r;
    r.method = "POST";
    r.path = path;
    r.query = query;
    r.content_length = static_cast<int64_t>(body.size());
    size_t pos = 0;
    int calls = 0;
    r.read_body = [&](char* buf, size_t size) -> int {
      if (fail_after >= 0 && calls++ >= fail_after)
        return -1;
      const size_t n = std::min({size, chunk, body.size() - pos});
      std::memcpy(buf, body.data() + pos, n);
      pos += n;
      return static_cast<int>(n);
    };
    FakeResponse resp;
    server_->handle(r, resp);
    return resp;
  }
};

}  // namespace

// ── Helpers ─────────────────────────────────────────────────────────────────

TEST(FileServerHelpers, UrlDecode) {
  EXPECT_EQ(FileServer::url_decode("%2Fbooks%2Fa+b.epub"), "/books/a b.epub");
  EXPECT_EQ(FileServer::url_decode("caf%C3%A9"), "caf\xc3\xa9");
  EXPECT_EQ(FileServer::url_decode("100%"), "100%");
  EXPECT_EQ(FileServer::url_decode("a%2"), "a%2");
  EXPECT_EQ(FileServer::url_decode("%zz%41"), "%zzA");
  EXPECT_EQ(FileServer::url_decode("%41"), "A");
}

TEST(FileServerHelpers, QueryParam) {
  std::string v;
  EXPECT_TRUE(FileServer::query_param("from=%2Fa&to=%2Fb%20c", "to", v));
  EXPECT_EQ(v, "/b c");
  EXPECT_TRUE(FileServer::query_param("from=%2Fa&to=%2Fb", "from", v));
  EXPECT_EQ(v, "/a");
  EXPECT_TRUE(FileServer::query_param("x&path=", "path", v));
  EXPECT_EQ(v, "");
  EXPECT_TRUE(FileServer::query_param("flag", "flag", v));
  EXPECT_FALSE(FileServer::query_param("pathx=1&xpath=2", "path", v));
  EXPECT_FALSE(FileServer::query_param("", "path", v));
}

TEST(FileServerHelpers, SafeRelativePath) {
  for (const char* ok : {"/", "/books", "/books/a.epub", "/.hidden", "/a/.../b", "/caf\xc3\xa9.epub", "/a b/c"})
    EXPECT_TRUE(FileServer::safe_relative_path(ok)) << ok;
  for (const char* bad : {"", "books", "/..", "/../x", "/a/../b", "/a/..", "/a/./b", "/.", "/a\\b", "/a\nb", "/a\x01"})
    EXPECT_FALSE(FileServer::safe_relative_path(bad)) << bad;
  EXPECT_FALSE(FileServer::safe_relative_path("/" + std::string(401, 'x')));
}

TEST(FileServerHelpers, HiddenParts) {
  EXPECT_TRUE(FileServer::has_hidden_part("/sdcard/.microreader/x.epub"));
  EXPECT_TRUE(FileServer::has_hidden_part("/.a"));
  EXPECT_TRUE(FileServer::has_hidden_part(".a"));
  EXPECT_FALSE(FileServer::has_hidden_part("/sdcard/books/a.b.epub"));
  EXPECT_FALSE(FileServer::has_hidden_part("/sdcard/books/"));
}

TEST(FileServerHelpers, JsonEscape) {
  EXPECT_EQ(FileServer::json_escape("a\"b\\c\nd\te\x01"), "a\\\"b\\\\c\\nd\\te\\u0001");
  EXPECT_EQ(FileServer::json_escape("caf\xc3\xa9"), "caf\xc3\xa9");  // UTF-8 passes through
}

// ── Page ────────────────────────────────────────────────────────────────────

TEST(WebPageEmbedded, MatchesIndexHtml) {
  // If this fails, run: python tools/embed_web_page.py
  const std::string src = read_file(fs::path(TEST_FIXTURES_DIR) / ".." / ".." / "lib" / "microreader" / "net" /
                                    "web" / "index.html");
  ASSERT_FALSE(src.empty());
  const uint32_t crc = static_cast<uint32_t>(
      mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(src.data()), src.size()));
  EXPECT_EQ(crc, kWebPageSourceCrc32);
  ASSERT_EQ(kWebPageSize, src.size());
  EXPECT_EQ(std::memcmp(kWebPage, src.data(), src.size()), 0);
}

TEST_F(FileServerTest, ServesThePageAndRedirectsEverythingElse) {
  FakeResponse r = get("/");
  EXPECT_EQ(r.code(), 200);
  EXPECT_EQ(r.content_type, "text/html; charset=utf-8");
  EXPECT_EQ(r.body.size(), kWebPageSize);
  EXPECT_NE(r.body.find("Sami Reader"), std::string::npos);
  EXPECT_EQ(r.finishes, 1);
  EXPECT_EQ(get("/index.html").code(), 200);

  // Phones' captive-portal checks (and typos) land on the page.
  for (const char* p : {"/generate_204", "/hotspot-detect.html", "/books/x"}) {
    FakeResponse rr = get(p);
    EXPECT_EQ(rr.code(), 302) << p;
    EXPECT_EQ(rr.headers["Location"], "/") << p;
  }
  EXPECT_EQ(post("/", "").code(), 405);
}

TEST_F(FileServerTest, StopsSendingThePageWhenTheClientLeaves) {
  HttpRequest req;
  req.method = "GET";
  req.path = "/";
  FakeResponse r;
  r.client_gone = true;
  server_->handle(req, r);
  EXPECT_EQ(r.writes, 1);
}

// ── API ─────────────────────────────────────────────────────────────────────

TEST_F(FileServerTest, InfoNamesTheReaderAndReportsStorageInKilobytes) {
  FakeResponse r = get("/api/info");
  EXPECT_EQ(r.code(), 200);
  EXPECT_EQ(r.content_type, "application/json");
  EXPECT_NE(r.body.find("\"name\":\"Sami Reader\""), std::string::npos) << r.body;
  EXPECT_NE(r.body.find("\"total\":33554432"), std::string::npos) << r.body;
  EXPECT_NE(r.body.find("\"free\":20971520"), std::string::npos) << r.body;
}

TEST_F(FileServerTest, ListsAFolder) {
  write_file(card() / "books" / "a.epub", std::string(2048, 'x'));
  write_file(card() / "books" / "quote\"name.txt", "hi");
  fs::create_directories(card() / "books" / "series");

  FakeResponse r = get("/api/list", "path=%2Fbooks");
  ASSERT_EQ(r.code(), 200) << r.body;
  EXPECT_EQ(r.body.front(), '[');
  EXPECT_EQ(r.body.back(), ']');
  EXPECT_NE(r.body.find("{\"name\":\"a.epub\",\"dir\":false,\"size\":2048,\"mtime\":"), std::string::npos) << r.body;
  EXPECT_NE(r.body.find("{\"name\":\"series\",\"dir\":true,\"size\":0,"), std::string::npos) << r.body;
  EXPECT_NE(r.body.find("\"name\":\"quote\\\"name.txt\""), std::string::npos) << r.body;
  EXPECT_EQ(count_of(r.body, "\"name\""), 3);

  // The card's root, and an empty folder.
  EXPECT_NE(get("/api/list", "path=%2F").body.find("\"name\":\"books\""), std::string::npos);
  EXPECT_EQ(get("/api/list", "path=%2Fbooks%2Fseries").body, "[]");
}

TEST_F(FileServerTest, ListsABigFolderInChunks) {
  for (int i = 0; i < 300; ++i)
    write_file(card() / "many" / ("book number " + std::to_string(i) + ".epub"), "x");
  FakeResponse r = get("/api/list", "path=%2Fmany");
  ASSERT_EQ(r.code(), 200);
  EXPECT_GT(r.writes, 5);  // streamed, not one big string
  EXPECT_EQ(count_of(r.body, "\"name\""), 300);
  EXPECT_EQ(count_of(r.body, "},{"), 299);
  EXPECT_EQ(r.body.front(), '[');
  EXPECT_EQ(r.body.back(), ']');
}

TEST_F(FileServerTest, ListErrors) {
  write_file(card() / "a.txt", "x");
  EXPECT_EQ(get("/api/list", "path=%2Fnope").code(), 404);
  EXPECT_EQ(get("/api/list", "path=%2Fa.txt").code(), 404);
  EXPECT_EQ(get("/api/list", "").code(), 400);
  EXPECT_EQ(get("/api/list", "path=books").code(), 400);
}

TEST_F(FileServerTest, UploadThenDownload) {
  fs::create_directories(card() / "books");
  std::string data;
  for (int i = 0; i < 10000; ++i)
    data.push_back(static_cast<char>(i * 7));

  FakeResponse up = post("/api/upload", "path=%2Fbooks%2FPiranesi%20(2020).epub", data, 777);
  ASSERT_EQ(up.code(), 200) << up.body;
  EXPECT_EQ(up.body, "{\"ok\":true}");
  EXPECT_EQ(read_file(card() / "books" / "Piranesi (2020).epub"), data);
  EXPECT_FALSE(fs::exists(card() / "books" / "Piranesi (2020).epub.part"));
  ASSERT_EQ(added_.size(), 1u);
  EXPECT_EQ(added_[0], full("/books/Piranesi (2020).epub"));
  ASSERT_EQ(activity_.size(), 1u);
  EXPECT_EQ(activity_[0], "Received Piranesi (2020).epub (10 KB)");
  EXPECT_EQ(busy_, (std::vector<bool>{true, false}));

  FakeResponse down = get("/api/file", "path=%2Fbooks%2FPiranesi%20(2020).epub");
  ASSERT_EQ(down.code(), 200);
  EXPECT_EQ(down.content_type, "application/octet-stream");
  EXPECT_EQ(down.headers["Content-Disposition"], "attachment; filename=\"Piranesi (2020).epub\"");
  EXPECT_EQ(down.body, data);
  EXPECT_EQ(activity_.back(), "Sent Piranesi (2020).epub");

  // Uploading again replaces the file.
  ASSERT_EQ(post("/api/upload", "path=%2Fbooks%2FPiranesi%20(2020).epub", "new").code(), 200);
  EXPECT_EQ(read_file(card() / "books" / "Piranesi (2020).epub"), "new");

  // Sizes over a megabyte read in MB, one decimal.
  ASSERT_EQ(post("/api/upload", "path=%2Fbig.bin", std::string(1536 * 1024, 'b'), 4096).code(), 200);
  EXPECT_EQ(activity_.back(), "Received big.bin (1.5 MB)");

  // An empty file is fine too.
  ASSERT_EQ(post("/api/upload", "path=%2Fempty.txt", "").code(), 200);
  EXPECT_TRUE(fs::exists(card() / "empty.txt"));
  EXPECT_EQ(fs::file_size(card() / "empty.txt"), 0u);
}

TEST_F(FileServerTest, FailedUploadLeavesNothingBehind) {
  fs::create_directories(card() / "books");
  write_file(card() / "books" / "keep.epub", "old");
  FakeResponse r = post("/api/upload", "path=%2Fbooks%2Fkeep.epub", std::string(10000, 'z'), 1000, /*fail_after=*/3);
  EXPECT_EQ(r.code(), 500);
  EXPECT_EQ(read_file(card() / "books" / "keep.epub"), "old");  // the old copy survives
  EXPECT_FALSE(fs::exists(card() / "books" / "keep.epub.part"));
  EXPECT_TRUE(added_.empty());
  EXPECT_EQ(busy_, (std::vector<bool>{true, false}));  // display updates resume

  EXPECT_EQ(post("/api/upload", "path=%2Fmissing%2Fa.epub", "x").code(), 404);
  EXPECT_EQ(post("/api/upload", "path=%2F", "x").code(), 400);
  EXPECT_EQ(post("/api/upload", "path=%2Fbooks%2F", "x").code(), 400);
  EXPECT_EQ(post("/api/upload", "path=%2Fbooks", "x").code(), 409);  // a folder has that name
  EXPECT_TRUE(fs::is_directory(card() / "books"));
}

TEST_F(FileServerTest, RejectsPathsOutsideTheCard) {
  write_file(root_ / "secret.txt", "secret");
  for (const char* q : {"path=%2F..%2Fsecret.txt", "path=/../secret.txt", "path=%2Fbooks%2F..%2F..%2Fsecret.txt",
                        "path=..%2Fsecret.txt", "path=%2F..%5Csecret.txt"}) {
    FakeResponse r = get("/api/file", q);
    EXPECT_EQ(r.code(), 400) << q;
    EXPECT_EQ(r.body, "{\"error\":\"Invalid path\"}") << q;
  }
  EXPECT_EQ(post("/api/delete", "path=%2F..").code(), 400);
  EXPECT_EQ(post("/api/upload", "path=%2F..%2Fevil.txt", "x").code(), 400);
  EXPECT_EQ(post("/api/rename", "from=%2F..%2Fsecret.txt&to=%2Fstolen.txt").code(), 400);
  EXPECT_EQ(post("/api/rename", "from=%2Fa&to=%2F..%2Fa").code(), 400);
  EXPECT_TRUE(fs::exists(root_ / "secret.txt"));
  EXPECT_FALSE(fs::exists(root_ / "evil.txt"));
  // The card itself can't be deleted or renamed.
  EXPECT_EQ(post("/api/delete", "path=%2F").code(), 404);
  EXPECT_EQ(post("/api/rename", "from=%2F&to=%2Fx").code(), 404);
  EXPECT_TRUE(fs::is_directory(card()));
}

TEST_F(FileServerTest, FoldersRenameAndDelete) {
  ASSERT_EQ(post("/api/mkdir", "path=%2FNew%20folder").code(), 200);
  EXPECT_TRUE(fs::is_directory(card() / "New folder"));
  EXPECT_EQ(activity_.back(), "Created folder New folder");
  EXPECT_EQ(post("/api/mkdir", "path=%2FNew%20folder").code(), 409);
  EXPECT_EQ(post("/api/mkdir", "path=%2Fmissing%2Fchild").code(), 500);

  // Rename a single book.
  write_file(card() / "books" / "a.epub", "A");
  ASSERT_EQ(post("/api/rename", "from=%2Fbooks%2Fa.epub&to=%2Fbooks%2Fb.epub").code(), 200);
  EXPECT_EQ(read_file(card() / "books" / "b.epub"), "A");
  ASSERT_EQ(renamed_.size(), 1u);
  EXPECT_EQ(renamed_[0], std::make_pair(full("/books/a.epub"), full("/books/b.epub")));
  EXPECT_EQ(activity_.back(), "Renamed a.epub to b.epub");
  EXPECT_EQ(post("/api/rename", "from=%2Fbooks%2Fnope.epub&to=%2Fbooks%2Fc.epub").code(), 404);
  write_file(card() / "books" / "c.epub", "C");
  EXPECT_EQ(post("/api/rename", "from=%2Fbooks%2Fb.epub&to=%2Fbooks%2Fc.epub").code(), 409);
  EXPECT_EQ(read_file(card() / "books" / "c.epub"), "C");  // not overwritten

  // Renaming a folder reports every book inside it (for the book list).
  write_file(card() / "Series" / "one.epub", "1");
  write_file(card() / "Series" / "Extras" / "two.EPUB", "2");
  write_file(card() / "Series" / "notes.txt", "n");
  renamed_.clear();
  ASSERT_EQ(post("/api/rename", "from=%2FSeries&to=%2FSaga").code(), 200);
  EXPECT_TRUE(fs::exists(card() / "Saga" / "Extras" / "two.EPUB"));
  ASSERT_EQ(renamed_.size(), 2u);
  std::sort(renamed_.begin(), renamed_.end());
  EXPECT_EQ(renamed_[0], std::make_pair(full("/Series/Extras/two.EPUB"), full("/Saga/Extras/two.EPUB")));
  EXPECT_EQ(renamed_[1], std::make_pair(full("/Series/one.epub"), full("/Saga/one.epub")));

  // Deleting a folder removes everything in it and reports each book.
  ASSERT_EQ(post("/api/delete", "path=%2FSaga").code(), 200);
  EXPECT_FALSE(fs::exists(card() / "Saga"));
  std::sort(removed_.begin(), removed_.end());
  EXPECT_EQ(removed_, (std::vector<std::string>{full("/Saga/Extras/two.EPUB"), full("/Saga/one.epub")}));
  EXPECT_EQ(activity_.back(), "Deleted Saga");

  // A single file (any kind) is reported as itself.
  removed_.clear();
  ASSERT_EQ(post("/api/delete", "path=%2Fbooks%2Fc.epub").code(), 200);
  EXPECT_EQ(removed_, (std::vector<std::string>{full("/books/c.epub")}));
  EXPECT_EQ(post("/api/delete", "path=%2Fbooks%2Fc.epub").code(), 404);
}

TEST_F(FileServerTest, UnknownRequests) {
  EXPECT_EQ(get("/api/nope").code(), 404);
  EXPECT_EQ(get("/api/upload", "path=%2Fa").code(), 404);  // uploads must be POSTs
  EXPECT_EQ(post("/api/list", "path=%2F").code(), 404);
  EXPECT_EQ(get("/api/rename", "from=%2Fa&to=%2Fb").code(), 404);
  FakeResponse r = get("/api/nope");
  EXPECT_EQ(r.content_type, "application/json");
  EXPECT_EQ(r.body, "{\"error\":\"Unknown request\"}");
}

// ── DNS / mDNS ──────────────────────────────────────────────────────────────

namespace {

void put16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x >> 8));
  v.push_back(static_cast<uint8_t>(x));
}
void put_name(std::vector<uint8_t>& v, const std::string& dotted) {
  size_t p = 0;
  while (p < dotted.size()) {
    size_t d = dotted.find('.', p);
    if (d == std::string::npos)
      d = dotted.size();
    v.push_back(static_cast<uint8_t>(d - p));
    v.insert(v.end(), dotted.begin() + static_cast<long>(p), dotted.begin() + static_cast<long>(d));
    p = d + 1;
  }
  v.push_back(0);
}
std::vector<uint8_t> query(uint16_t id, uint16_t flags, const std::string& name, uint16_t type, uint16_t cls = 1) {
  std::vector<uint8_t> q;
  put16(q, id);
  put16(q, flags);
  put16(q, 1);
  put16(q, 0);
  put16(q, 0);
  put16(q, 0);
  put_name(q, name);
  put16(q, type);
  put16(q, cls);
  return q;
}
uint16_t rd16(const uint8_t* p) {
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
const uint8_t kIp[4] = {192, 168, 1, 42};

}  // namespace

TEST(Dns, ReadsCompressedNamesAndRejectsLoops) {
  std::vector<uint8_t> m(12, 0);
  put_name(m, "sami.local");  // at 12
  m.push_back(0xC0);          // pointer to it at 24
  m.push_back(12);
  std::string name;
  size_t next = 0;
  ASSERT_TRUE(net::dns_read_name(m.data(), m.size(), 24, name, next));
  EXPECT_EQ(name, "sami.local");
  EXPECT_EQ(next, 26u);

  std::vector<uint8_t> loop(12, 0);
  loop.push_back(0xC0);
  loop.push_back(12);  // points at itself
  EXPECT_FALSE(net::dns_read_name(loop.data(), loop.size(), 12, name, next));
  std::vector<uint8_t> trunc(12, 0);
  trunc.push_back(5);
  trunc.push_back('s');
  EXPECT_FALSE(net::dns_read_name(trunc.data(), trunc.size(), 12, name, next));
}

TEST(Dns, AnswersMdnsForSamiLocal) {
  const auto q = query(0x1234, 0, "Sami.LOCAL", 1);
  uint8_t out[512];
  bool unicast = true;
  const size_t n = mdns_answer(q.data(), q.size(), "sami.local", kIp, false, out, sizeof(out), &unicast);
  ASSERT_GT(n, 12u);
  EXPECT_FALSE(unicast);
  EXPECT_EQ(rd16(out), 0);  // mDNS: ID 0
  EXPECT_EQ(rd16(out + 2), 0x8400);
  EXPECT_EQ(rd16(out + 4), 0);  // no question
  EXPECT_EQ(rd16(out + 6), 1);  // one answer
  std::string name;
  size_t next = 0;
  ASSERT_TRUE(net::dns_read_name(out, n, 12, name, next));
  EXPECT_EQ(name, "sami.local");
  EXPECT_EQ(rd16(out + next), 1);           // A
  EXPECT_EQ(rd16(out + next + 2), 0x8001);  // IN, cache-flush
  EXPECT_EQ(rd16(out + next + 6), 120);     // TTL (low half)
  EXPECT_EQ(rd16(out + next + 8), 4);
  EXPECT_EQ(std::memcmp(out + next + 10, kIp, 4), 0);
  EXPECT_EQ(n, next + 14);

  // ANY works; a "unicast response" (QU) question asks for a direct reply.
  const auto qu = query(0, 0, "sami.local", 255, 0x8001);
  ASSERT_GT(mdns_answer(qu.data(), qu.size(), "sami.local", kIp, false, out, sizeof(out), &unicast), 0u);
  EXPECT_TRUE(unicast);
}

TEST(Dns, IgnoresOtherMdnsTraffic) {
  uint8_t out[512];
  bool unicast = false;
  const auto other = query(0, 0, "printer.local", 1);
  EXPECT_EQ(mdns_answer(other.data(), other.size(), "sami.local", kIp, false, out, sizeof(out), &unicast), 0u);
  const auto aaaa = query(0, 0, "sami.local", 28);
  EXPECT_EQ(mdns_answer(aaaa.data(), aaaa.size(), "sami.local", kIp, false, out, sizeof(out), &unicast), 0u);
  const auto response = query(0, 0x8400, "sami.local", 1);
  EXPECT_EQ(mdns_answer(response.data(), response.size(), "sami.local", kIp, false, out, sizeof(out), &unicast), 0u);
  auto cut = query(0, 0, "sami.local", 1);
  cut.resize(cut.size() - 3);
  EXPECT_EQ(mdns_answer(cut.data(), cut.size(), "sami.local", kIp, false, out, sizeof(out), &unicast), 0u);
  EXPECT_EQ(mdns_answer(cut.data(), 5, "sami.local", kIp, false, out, sizeof(out), &unicast), 0u);
  // Too little room for the reply.
  const auto q = query(0, 0, "sami.local", 1);
  EXPECT_EQ(mdns_answer(q.data(), q.size(), "sami.local", kIp, false, out, 20, &unicast), 0u);
}

TEST(Dns, FindsSamiAmongSeveralCompressedQuestions) {
  std::vector<uint8_t> q;
  put16(q, 0);
  put16(q, 0);
  put16(q, 2);
  put16(q, 0);
  put16(q, 0);
  put16(q, 0);
  put_name(q, "other.local");  // 12..24
  put16(q, 1);
  put16(q, 1);
  q.push_back(4);  // "sami" + pointer to ".local" in the first name
  q.insert(q.end(), {'s', 'a', 'm', 'i'});
  q.push_back(0xC0);
  q.push_back(18);
  put16(q, 1);
  put16(q, 1);
  uint8_t out[512];
  bool unicast = false;
  EXPECT_GT(mdns_answer(q.data(), q.size(), "sami.local", kIp, false, out, sizeof(out), &unicast), 0u);
}

TEST(Dns, LegacyResolversGetTheirIdAndQuestionBack) {
  const auto q = query(0xBEEF, 0x0100, "sami.local", 1);
  uint8_t out[512];
  bool unicast = false;
  const size_t n = mdns_answer(q.data(), q.size(), "sami.local", kIp, true, out, sizeof(out), &unicast);
  ASSERT_GT(n, 0u);
  EXPECT_TRUE(unicast);
  EXPECT_EQ(rd16(out), 0xBEEF);
  EXPECT_EQ(rd16(out + 4), 1);
  EXPECT_EQ(rd16(out + 6), 1);
  std::string name;
  size_t next = 0;
  ASSERT_TRUE(net::dns_read_name(out, n, 12, name, next));  // question
  ASSERT_TRUE(net::dns_read_name(out, n, next + 4, name, next));  // answer
  EXPECT_EQ(rd16(out + next + 2), 1);   // plain IN, no cache-flush bit
  EXPECT_EQ(rd16(out + next + 6), 10);  // short TTL
}

TEST(Dns, AnnouncesTheAddress) {
  uint8_t out[512];
  const size_t n = mdns_announce("sami.local", kIp, out, sizeof(out));
  ASSERT_GT(n, 0u);
  EXPECT_EQ(rd16(out + 2), 0x8400);
  EXPECT_EQ(rd16(out + 6), 1);
  EXPECT_EQ(std::memcmp(out + n - 4, kIp, 4), 0);
}

TEST(Dns, CaptiveDnsAnswersEveryName) {
  const auto q = query(0x4242, 0x0100, "connectivitycheck.gstatic.com", 1);
  uint8_t out[512];
  const size_t n = dns_answer_all(q.data(), q.size(), kIp, out, sizeof(out));
  ASSERT_EQ(n, q.size() + 16);
  EXPECT_EQ(rd16(out), 0x4242);
  EXPECT_EQ(rd16(out + 2), 0x8580);  // response, authoritative, RD copied, RA
  EXPECT_EQ(rd16(out + 4), 1);
  EXPECT_EQ(rd16(out + 6), 1);
  EXPECT_EQ(std::memcmp(out + 12, q.data() + 12, q.size() - 12), 0);  // question echoed
  const uint8_t* a = out + q.size();
  EXPECT_EQ(rd16(a), 0xC00C);
  EXPECT_EQ(rd16(a + 2), 1);
  EXPECT_EQ(std::memcmp(a + 12, kIp, 4), 0);

  // Other record types: an empty answer (so phones fall back to IPv4).
  const auto aaaa = query(1, 0x0100, "example.com", 28);
  const size_t m = dns_answer_all(aaaa.data(), aaaa.size(), kIp, out, sizeof(out));
  ASSERT_EQ(m, aaaa.size());
  EXPECT_EQ(rd16(out + 6), 0);

  // Garbage and responses are ignored.
  const auto resp = query(1, 0x8180, "example.com", 1);
  EXPECT_EQ(dns_answer_all(resp.data(), resp.size(), kIp, out, sizeof(out)), 0u);
  EXPECT_EQ(dns_answer_all(q.data(), 11, kIp, out, sizeof(out)), 0u);
  EXPECT_EQ(dns_answer_all(q.data(), q.size() - 2, kIp, out, sizeof(out)), 0u);
}

// ── Saved networks ──────────────────────────────────────────────────────────

TEST(SavedWifiTest, RemembersMostRecentFirstAndForgets) {
  const fs::path dir = fs::temp_directory_path() / "saved_wifi_test";
  fs::remove_all(dir);
  fs::create_directories(dir);
  const std::string file = (dir / "wifi.txt").string();

  SavedWifi w;
  w.set_file(file);
  w.load();  // no file yet
  EXPECT_TRUE(w.networks().empty());
  w.remember("Home", "correct horse");
  w.remember("Cafe Wi-Fi", "");
  w.remember("Home", "battery staple");  // moves to the front with the new password
  ASSERT_EQ(w.networks().size(), 2u);
  EXPECT_EQ(w.networks()[0].ssid, "Home");

  SavedWifi again;
  again.set_file(file);
  again.load();
  std::string pw;
  ASSERT_TRUE(again.find("Home", pw));
  EXPECT_EQ(pw, "battery staple");
  ASSERT_TRUE(again.find("Cafe Wi-Fi", pw));
  EXPECT_EQ(pw, "");
  EXPECT_FALSE(again.find("home", pw));  // names are case-sensitive

  again.forget("Home");
  SavedWifi third;
  third.set_file(file);
  third.load();
  EXPECT_FALSE(third.find("Home", pw));
  EXPECT_TRUE(third.find("Cafe Wi-Fi", pw));

  // Names or passwords that would break the file format are not saved.
  third.remember("Bad\tName", "x");
  third.remember("Fine", "bad\npassword");
  EXPECT_EQ(third.networks().size(), 1u);

  // At most kMax networks.
  for (int i = 0; i < 12; ++i)
    third.remember("Net " + std::to_string(i), "pw");
  EXPECT_EQ(third.networks().size(), SavedWifi::kMax);
  EXPECT_EQ(third.networks()[0].ssid, "Net 11");
  fs::remove_all(dir);
}
