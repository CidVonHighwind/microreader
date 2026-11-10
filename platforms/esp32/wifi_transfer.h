#pragma once

// Wi-Fi File Transfer for the ESP32-C3 (Sami Reader).
//
// Joins a Wi-Fi network (or starts a "Sami Reader" hotspot) and serves the
// file manager page from lib/microreader/net/FileServer on port 80. The page
// is at http://sami.local (a tiny mDNS responder) or the IP address shown on
// the reader. In hotspot mode a captive DNS answers every name with the
// reader's address, so phones open the page by themselves.
//
// Header-only and included by main.cpp only: it shares the book-index queue
// and the "pause the display" flag with serial_communication.h, whose
// variables are file-static.
//
// Threads: the main loop calls scan/start_*/stop/poll (via the Wi-Fi screens);
// esp_http_server runs requests in its own task; a small task answers DNS.

#include <sys/param.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "microreader/content/BookIndex.h"
#include "microreader/net/Dns.h"
#include "microreader/net/FileServer.h"
#include "microreader/net/WifiTransfer.h"
#include "nvs_flash.h"
#include "serial_communication.h"

// True while the main loop is inside app.update() (drawing to the display).
// Requests wait for it to clear before touching the SD card, which shares the
// SPI bus with the display.
static volatile bool g_ui_drawing = false;

class Esp32WifiTransfer final : public microreader::IWifiTransfer {
 public:
  static constexpr const char* kTag = "wifi";
  static constexpr const char* kRoot = "/sdcard";

  Esp32WifiTransfer() {
    activity_mutex_ = xSemaphoreCreateMutex();
  }

  // ── IWifiTransfer ─────────────────────────────────────────────────────────

  bool scan(std::vector<microreader::WifiNetwork>& out) override {
    out.clear();
    if (!start_radio_(WIFI_MODE_STA))
      return false;
    wifi_scan_config_t cfg = {};
    cfg.show_hidden = false;
    if (esp_wifi_scan_start(&cfg, /*block=*/true) != ESP_OK) {
      ESP_LOGW(kTag, "scan failed");
      return false;
    }
    uint16_t n = 24;
    std::vector<wifi_ap_record_t> recs(n);
    if (esp_wifi_scan_get_ap_records(&n, recs.data()) != ESP_OK)
      return false;
    recs.resize(n);
    for (const auto& r : recs) {  // strongest first
      const char* ssid = reinterpret_cast<const char*>(r.ssid);
      // 5 = WIFI_AUTH_WPA2_ENTERPRISE (renamed WIFI_AUTH_ENTERPRISE in newer IDFs)
      if (!ssid[0] || static_cast<int>(r.authmode) == 5)
        continue;  // hidden, or needs a user name too
      bool dup = false;
      for (const auto& o : out)
        dup = dup || o.ssid == ssid;
      if (dup)
        continue;
      microreader::WifiNetwork w;
      w.ssid = ssid;
      w.rssi = r.rssi;
      w.secured = r.authmode != WIFI_AUTH_OPEN && r.authmode != WIFI_AUTH_OWE;
      out.push_back(std::move(w));
      if (out.size() >= 15)
        break;
    }
    ESP_LOGI(kTag, "scan: %u networks", (unsigned)out.size());
    return true;
  }

  bool start_join(const std::string& ssid, const std::string& password) override {
    stop_servers_();
    hotspot_ = false;
    ssid_ = ssid;
    attempts_ = 0;
    auth_fails_ = 0;
    failed_auth_ = false;
    failure_.clear();
    ip_.clear();
    set_state_(microreader::WifiState::Connecting);
    deadline_ms_ = now_ms_() + 30000;

    if (!init_driver_()) {
      fail_("Wi-Fi didn't start (not enough memory?)");
      return false;
    }
    if (radio_on_) {
      esp_wifi_stop();
      radio_on_ = false;
    }
    wifi_config_t cfg = {};
    std::strncpy(reinterpret_cast<char*>(cfg.sta.ssid), ssid.c_str(), sizeof(cfg.sta.ssid));
    std::strncpy(reinterpret_cast<char*>(cfg.sta.password), password.c_str(), sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    ev_sta_start_ = false;
    ev_got_ip_ = false;
    ev_disconnected_ = false;
    esp_netif_set_hostname(sta_netif_, microreader::kWifiHostname);
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK ||
        esp_wifi_start() != ESP_OK) {
      fail_("Wi-Fi didn't start");
      return false;
    }
    radio_on_ = true;
    esp_wifi_set_ps(WIFI_PS_NONE);  // faster transfers; it's only on briefly
    ESP_LOGI(kTag, "joining '%s'", ssid.c_str());
    return true;  // poll() connects once the station has started
  }

  bool start_hotspot(const std::string& ssid, const std::string& password) override {
    stop_servers_();
    hotspot_ = true;
    ssid_ = ssid;
    failed_auth_ = false;
    failure_.clear();
    ip_.clear();
    set_state_(microreader::WifiState::Connecting);

    if (!init_driver_()) {
      fail_("Wi-Fi didn't start (not enough memory?)");
      return false;
    }
    if (radio_on_) {
      esp_wifi_stop();
      radio_on_ = false;
    }
    wifi_config_t cfg = {};
    std::strncpy(reinterpret_cast<char*>(cfg.ap.ssid), ssid.c_str(), sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = static_cast<uint8_t>(MIN(ssid.size(), sizeof(cfg.ap.ssid)));
    std::strncpy(reinterpret_cast<char*>(cfg.ap.password), password.c_str(), sizeof(cfg.ap.password));
    cfg.ap.channel = 6;
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = password.size() >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.ap.pmf_cfg.required = false;

    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK || esp_wifi_set_config(WIFI_IF_AP, &cfg) != ESP_OK) {
      fail_("Couldn't set up the hotspot");
      return false;
    }
    offer_own_dns_();
    if (esp_wifi_start() != ESP_OK) {
      fail_("Couldn't start the hotspot");
      return false;
    }
    radio_on_ = true;
    esp_wifi_set_ps(WIFI_PS_NONE);

    esp_netif_ip_info_t info = {};
    esp_netif_get_ip_info(ap_netif_, &info);
    if (!start_servers_(info.ip.addr)) {
      fail_("Couldn't start the web page");
      return false;
    }
    ip_ = ip_text_(info.ip.addr);
    set_state_(microreader::WifiState::Hotspot);
    ESP_LOGI(kTag, "hotspot '%s' at %s", ssid.c_str(), ip_.c_str());
    log_heap_("hotspot up");
    return true;
  }

  void stop() override {
    if (!inited_ && !radio_on_ && state_ == microreader::WifiState::Off)
      return;
    stopping_ = true;
    stop_servers_();
    if (radio_on_) {
      esp_wifi_stop();
      radio_on_ = false;
    }
    deinit_driver_();
    if (busy_) {
      busy_ = false;
      g_upload_in_progress = false;
    }
    ip_.clear();
    set_state_(microreader::WifiState::Off);
    stopping_ = false;
    log_heap_("wifi off");
  }

  void poll() override {
    if (hotspot_ || !radio_on_)
      return;
    using microreader::WifiState;
    const WifiState st = state_;

    if (ev_sta_start_) {
      ev_sta_start_ = false;
      esp_wifi_connect();
    }
    if (ev_got_ip_) {
      ev_got_ip_ = false;
      const uint32_t addr = got_ip_;
      ip_ = ip_text_(addr);
      if (!servers_running_ && !start_servers_(addr)) {
        fail_("Couldn't start the web page");
        return;
      }
      attempts_ = 0;
      auth_fails_ = 0;
      set_state_(WifiState::Connected);
      ESP_LOGI(kTag, "connected, %s", ip_.c_str());
      log_heap_("joined");
      return;
    }
    if (ev_disconnected_) {
      ev_disconnected_ = false;
      const int reason = disconnect_reason_;
      ESP_LOGI(kTag, "disconnected, reason %d", reason);
      if (st == WifiState::Connected) {
        // Dropped out: keep the page running and reconnect.
        set_state_(WifiState::Connecting);
        deadline_ms_ = now_ms_() + 30000;
        attempts_ = 0;
        esp_wifi_connect();
        return;
      }
      if (st != WifiState::Connecting)
        return;
      const bool auth = reason == 15 || reason == 202 || reason == 204;  // handshake/auth failed
      const bool not_found = reason == 201 || reason == 210 || reason == 211 || reason == 212;
      if (auth && ++auth_fails_ >= 2) {
        failed_auth_ = true;
        fail_("Wrong password?");
        return;
      }
      if (++attempts_ >= 4) {
        if (not_found)
          fail_("Network not found. Is it 2.4 GHz?");
        else if (auth)
          fail_("Wrong password?");
        else {
          char why[48];
          std::snprintf(why, sizeof(why), "Couldn't connect (code %d)", reason);
          fail_(why);
        }
        return;
      }
      esp_wifi_connect();  // try again
      return;
    }
    if (st == WifiState::Connecting && now_ms_() > deadline_ms_)
      fail_("No answer from the network");
  }

  microreader::WifiState state() const override {
    return state_;
  }
  std::string ip_address() const override {
    return ip_;
  }
  std::string failure() const override {
    return failure_;
  }
  bool failed_auth() const override {
    return failed_auth_;
  }
  std::string activity() const override {
    std::string s;
    xSemaphoreTake(activity_mutex_, portMAX_DELAY);
    s = activity_;
    xSemaphoreGive(activity_mutex_);
    return s;
  }
  uint32_t activity_count() const override {
    return activity_count_;
  }

 private:
  // ── Driver ────────────────────────────────────────────────────────────────

  static uint32_t now_ms_() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
  }
  static std::string ip_text_(uint32_t addr) {  // network byte order
    char b[16];
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&addr);
    std::snprintf(b, sizeof(b), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
    return b;
  }
  static void log_heap_(const char* when) {
    ESP_LOGI(kTag, "%s: heap free=%lu largest=%lu", when, (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  }

  void set_state_(microreader::WifiState s) {
    state_ = s;
  }
  void fail_(const char* why) {
    ESP_LOGW(kTag, "failed: %s", why);
    failure_ = why;
    stop_servers_();
    if (radio_on_) {
      esp_wifi_stop();
      radio_on_ = false;
    }
    set_state_(microreader::WifiState::Failed);
  }

  // One-time and per-session setup. The driver is torn down again in stop()
  // so its memory (~50 KB) is free for reading.
  bool init_driver_() {
    if (inited_)
      return true;
    if (!base_ready_) {
      esp_err_t err = nvs_flash_init();
      if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
      }
      if (err != ESP_OK) {
        ESP_LOGE(kTag, "nvs init: %s", esp_err_to_name(err));
        return false;
      }
      if (esp_netif_init() != ESP_OK)
        return false;
      err = esp_event_loop_create_default();
      if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        return false;
      base_ready_ = true;
    }
    log_heap_("before wifi init");
    sta_netif_ = esp_netif_create_default_wifi_sta();
    ap_netif_ = esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
      ESP_LOGE(kTag, "esp_wifi_init failed");
      destroy_netifs_();
      return false;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);  // don't write network details to flash
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_event_, this, &wifi_handler_);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_event_, this, &ip_handler_);
    inited_ = true;
    return true;
  }

  void deinit_driver_() {
    if (!inited_)
      return;
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler_);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler_);
    esp_wifi_deinit();
    destroy_netifs_();
    inited_ = false;
  }

  void destroy_netifs_() {
    if (sta_netif_)
      esp_netif_destroy_default_wifi(sta_netif_);
    if (ap_netif_)
      esp_netif_destroy_default_wifi(ap_netif_);
    sta_netif_ = ap_netif_ = nullptr;
  }

  bool start_radio_(wifi_mode_t mode) {
    if (!init_driver_())
      return false;
    wifi_mode_t cur = WIFI_MODE_NULL;
    esp_wifi_get_mode(&cur);
    if (radio_on_ && cur == mode)
      return true;
    if (radio_on_) {
      esp_wifi_stop();
      radio_on_ = false;
    }
    if (esp_wifi_set_mode(mode) != ESP_OK || esp_wifi_start() != ESP_OK)
      return false;
    radio_on_ = true;
    return true;
  }

  // Hotspot: tell phones (via DHCP) to use the reader as their DNS server,
  // so the captive DNS below can point every name at the page.
  void offer_own_dns_() {
    esp_netif_ip_info_t info = {};
    esp_netif_get_ip_info(ap_netif_, &info);
    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = info.ip.addr;
    uint8_t offer = 0x02;  // OFFER_DNS
    esp_netif_dhcps_stop(ap_netif_);
    esp_netif_dhcps_option(ap_netif_, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
    esp_netif_set_dns_info(ap_netif_, ESP_NETIF_DNS_MAIN, &dns);
    esp_netif_dhcps_start(ap_netif_);
  }

  static void on_event_(void* arg, esp_event_base_t base, int32_t id, void* data) {
    // Runs in the system event task (small stack): just note what happened.
    auto* self = static_cast<Esp32WifiTransfer*>(arg);
    if (base == WIFI_EVENT) {
      if (id == WIFI_EVENT_STA_START) {
        self->ev_sta_start_ = true;
      } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        self->disconnect_reason_ = static_cast<const wifi_event_sta_disconnected_t*>(data)->reason;
        self->ev_disconnected_ = true;
      } else if (id == WIFI_EVENT_AP_STACONNECTED) {
        self->note_("A device joined the hotspot");
      }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
      self->got_ip_ = static_cast<const ip_event_got_ip_t*>(data)->ip_info.ip.addr;
      self->ev_got_ip_ = true;
    }
  }

  // ── Web page and DNS ──────────────────────────────────────────────────────

  bool start_servers_(uint32_t ip) {
    if (servers_running_)
      return true;
    server_ip_ = ip;
    if (!server_) {
      microreader::net::FileServerHooks h;
      h.file_added = [this](const std::string& p) {
        if (indexable_(p))
          queue_index_op_(SerialIndexOp::Add, p);
      };
      h.file_removed = [this](const std::string& p) {
        if (microreader::BookIndex::is_book_path(p.c_str()) && p.size() < sizeof(g_index_path_a))
          queue_index_op_(SerialIndexOp::Remove, p);
      };
      h.file_renamed = [this](const std::string& a, const std::string& b) {
        const bool was = microreader::BookIndex::is_book_path(a.c_str()) && a.size() < sizeof(g_index_path_a);
        const bool is = indexable_(b);
        if (was && is)
          queue_index_op_(SerialIndexOp::Rename, a, b);
        else if (was)
          queue_index_op_(SerialIndexOp::Remove, a);
        else if (is)
          queue_index_op_(SerialIndexOp::Add, b);
      };
      h.activity = [this](const std::string& t) { note_(t); };
      h.storage = [](uint64_t& total, uint64_t& free_b) {
        return esp_vfs_fat_info(kRoot, &total, &free_b) == ESP_OK;
      };
      server_ = new microreader::net::FileServer(kRoot, std::move(h));
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.stack_size = 6144;
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 10;
    cfg.max_uri_handlers = 4;
    if (httpd_start(&httpd_, &cfg) != ESP_OK) {
      httpd_ = nullptr;
      return false;
    }
    httpd_uri_t get = {};
    get.uri = "/*";
    get.method = HTTP_GET;
    get.handler = &on_http_;
    get.user_ctx = this;
    httpd_uri_t post = get;
    post.method = HTTP_POST;
    httpd_register_uri_handler(httpd_, &get);
    httpd_register_uri_handler(httpd_, &post);

    dns_stop_ = false;
    dns_running_ = true;
    if (xTaskCreate(&dns_task_, "sami_dns", 3072, this, 4, nullptr) != pdPASS)
      dns_running_ = false;  // the page still works by IP address
    servers_running_ = true;
    return true;
  }

  void stop_servers_() {
    if (!servers_running_)
      return;
    stopping_ = true;
    if (httpd_) {
      httpd_stop(httpd_);
      httpd_ = nullptr;
    }
    dns_stop_ = true;
    for (int i = 0; i < 100 && dns_running_; ++i)
      vTaskDelay(pdMS_TO_TICKS(10));
    servers_running_ = false;
    stopping_ = false;
  }

  bool indexable_(const std::string& p) const {
    // Same rule as the book list's own scan: EPUBs outside hidden folders.
    return microreader::BookIndex::is_book_path(p.c_str()) &&
           !microreader::net::FileServer::has_hidden_part(p) && p.size() < sizeof(g_index_path_a);
  }

  // The book index is only changed from the main loop; queue the change and
  // wait for a free slot rather than dropping it.
  void queue_index_op_(SerialIndexOp op, const std::string& a, const std::string& b = std::string()) {
    for (int i = 0; i < 500 && g_index_op != SerialIndexOp::None && !stopping_; ++i)
      vTaskDelay(pdMS_TO_TICKS(20));
    request_index_op(op, a.c_str(), b.empty() ? nullptr : b.c_str());
  }

  void note_(const std::string& text) {
    xSemaphoreTake(activity_mutex_, portMAX_DELAY);
    activity_ = text;
    xSemaphoreGive(activity_mutex_);
    activity_count_ = activity_count_ + 1;
  }

  // Pause display updates while the SD card is in use (shared SPI bus).
  void set_busy_(bool on) {
    if (on) {
      busy_ = true;
      g_upload_in_progress = true;
      for (int i = 0; i < 300 && g_ui_drawing && !stopping_; ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    } else if (busy_) {
      busy_ = false;
      g_upload_in_progress = false;
    }
  }

  class Response final : public microreader::net::HttpResponse {
   public:
    explicit Response(httpd_req_t* r) : r_(r) {}
    void begin(const char* status, const char* type) override {
      status_ = status;
      type_ = type;
      httpd_resp_set_status(r_, status_.c_str());
      httpd_resp_set_type(r_, type_.c_str());
    }
    void header(const char* name, const char* value) override {
      // esp_http_server keeps the pointers until the response is sent.
      strings_.emplace_back(name);
      const char* n = strings_.back().c_str();
      strings_.emplace_back(value);
      httpd_resp_set_hdr(r_, n, strings_.back().c_str());
    }
    bool write(const char* data, size_t size) override {
      if (!ok_)
        return false;
      if (size == 0)
        return true;
      chunked_ = true;
      ok_ = httpd_resp_send_chunk(r_, data, static_cast<ssize_t>(size)) == ESP_OK;
      return ok_;
    }
    void finish() override {
      if (finished_)
        return;
      finished_ = true;
      if (chunked_) {
        if (ok_)
          ok_ = httpd_resp_send_chunk(r_, nullptr, 0) == ESP_OK;
      } else {
        ok_ = httpd_resp_send(r_, nullptr, 0) == ESP_OK;
      }
    }
    bool ok() const {
      return ok_;
    }

   private:
    httpd_req_t* r_;
    std::string status_, type_;
    std::deque<std::string> strings_;
    bool chunked_ = false;
    bool finished_ = false;
    bool ok_ = true;
  };

  static esp_err_t on_http_(httpd_req_t* r) {
    return static_cast<Esp32WifiTransfer*>(r->user_ctx)->handle_(r);
  }

  esp_err_t handle_(httpd_req_t* r) {
    microreader::net::HttpRequest req;
    req.method = r->method == HTTP_POST ? "POST" : "GET";
    const char* uri = r->uri;
    const char* q = std::strchr(uri, '?');
    req.path = q ? std::string(uri, static_cast<size_t>(q - uri)) : std::string(uri);
    req.query = q ? std::string(q + 1) : std::string();
    req.content_length = static_cast<int64_t>(r->content_len);
    req.read_body = [this, r](char* buf, size_t size) -> int {
      for (int tries = 0; tries < 3; ++tries) {
        if (stopping_)
          return -1;
        const int got = httpd_req_recv(r, buf, size);
        if (got == HTTPD_SOCK_ERR_TIMEOUT)
          continue;
        return got;
      }
      return -1;
    };

    const bool api = req.path.rfind("/api/", 0) == 0;
    if (req.path == "/" && req.method == "GET") {
      note_("A browser opened the page");
    } else if (req.path == "/api/list") {
      std::string p;
      microreader::net::FileServer::query_param(req.query, "path", p);
      note_(p == "/" ? std::string("Browsing the SD card") : "Browsing " + p);
    }

    Response resp(r);
    if (api)
      set_busy_(true);
    server_->handle(req, resp);
    if (api)
      set_busy_(false);
    resp.finish();
    return resp.ok() ? ESP_OK : ESP_FAIL;
  }

  static void dns_task_(void* arg) {
    static_cast<Esp32WifiTransfer*>(arg)->dns_loop_();
    vTaskDelete(nullptr);
  }

  // mDNS for "sami.local" (both modes) and, on the hotspot, a DNS server that
  // answers every name with the reader's address.
  void dns_loop_() {
    char host[32];
    std::snprintf(host, sizeof(host), "%s.local", microreader::kWifiHostname);
    const uint32_t ip = server_ip_;
    const uint8_t* ipb = reinterpret_cast<const uint8_t*>(&ip);

    int mdns = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (mdns >= 0) {
      int yes = 1;
      setsockopt(mdns, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
      sockaddr_in a = {};
      a.sin_family = AF_INET;
      a.sin_port = htons(microreader::net::kMdnsPort);
      a.sin_addr.s_addr = htonl(INADDR_ANY);
      ip_mreq m = {};
      std::memcpy(&m.imr_multiaddr.s_addr, microreader::net::kMdnsGroup, 4);
      m.imr_interface.s_addr = ip;
      in_addr iface = {};
      iface.s_addr = ip;
      uint8_t ttl = 255;
      if (bind(mdns, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 ||
          setsockopt(mdns, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof(m)) != 0) {
        ESP_LOGW(kTag, "mDNS unavailable");
        close(mdns);
        mdns = -1;
      } else {
        setsockopt(mdns, IPPROTO_IP, IP_MULTICAST_IF, &iface, sizeof(iface));
        setsockopt(mdns, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
      }
    }
    int dns = -1;
    if (hotspot_) {
      dns = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      sockaddr_in a = {};
      a.sin_family = AF_INET;
      a.sin_port = htons(53);
      a.sin_addr.s_addr = htonl(INADDR_ANY);
      if (dns >= 0 && bind(dns, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        close(dns);
        dns = -1;
      }
    }

    sockaddr_in group = {};
    group.sin_family = AF_INET;
    group.sin_port = htons(microreader::net::kMdnsPort);
    std::memcpy(&group.sin_addr.s_addr, microreader::net::kMdnsGroup, 4);

    int announces = 0;
    uint32_t next_announce = now_ms_();
    while (!dns_stop_) {
      if (mdns >= 0 && announces < 3 && now_ms_() >= next_announce) {
        const size_t n = microreader::net::mdns_announce(host, ipb, tx_, sizeof(tx_));
        if (n)
          sendto(mdns, tx_, n, 0, reinterpret_cast<sockaddr*>(&group), sizeof(group));
        ++announces;
        next_announce = now_ms_() + 1000u * announces;
      }
      fd_set fds;
      FD_ZERO(&fds);
      int maxfd = -1;
      if (mdns >= 0) {
        FD_SET(mdns, &fds);
        maxfd = MAX(maxfd, mdns);
      }
      if (dns >= 0) {
        FD_SET(dns, &fds);
        maxfd = MAX(maxfd, dns);
      }
      if (maxfd < 0)
        break;
      timeval tv = {0, 250000};
      if (select(maxfd + 1, &fds, nullptr, nullptr, &tv) <= 0)
        continue;
      sockaddr_in from = {};
      socklen_t from_len = sizeof(from);
      if (mdns >= 0 && FD_ISSET(mdns, &fds)) {
        const int got =
            recvfrom(mdns, rx_, sizeof(rx_), 0, reinterpret_cast<sockaddr*>(&from), &from_len);
        if (got > 0) {
          bool unicast = false;
          const bool legacy = ntohs(from.sin_port) != microreader::net::kMdnsPort;
          const size_t n = microreader::net::mdns_answer(rx_, static_cast<size_t>(got), host, ipb, legacy, tx_,
                                                         sizeof(tx_), &unicast);
          if (n) {
            const sockaddr_in& to = unicast ? from : group;
            sendto(mdns, tx_, n, 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
          }
        }
      }
      if (dns >= 0 && FD_ISSET(dns, &fds)) {
        from_len = sizeof(from);
        const int got = recvfrom(dns, rx_, sizeof(rx_), 0, reinterpret_cast<sockaddr*>(&from), &from_len);
        if (got > 0) {
          const size_t n = microreader::net::dns_answer_all(rx_, static_cast<size_t>(got), ipb, tx_, sizeof(tx_));
          if (n)
            sendto(dns, tx_, n, 0, reinterpret_cast<sockaddr*>(&from), from_len);
        }
      }
    }
    if (mdns >= 0)
      close(mdns);
    if (dns >= 0)
      close(dns);
    dns_running_ = false;
  }

  // ── State ─────────────────────────────────────────────────────────────────

  bool base_ready_ = false;  // NVS, netif and event loop (once per boot)
  bool inited_ = false;      // Wi-Fi driver
  bool radio_on_ = false;
  esp_netif_t* sta_netif_ = nullptr;
  esp_netif_t* ap_netif_ = nullptr;
  esp_event_handler_instance_t wifi_handler_ = nullptr;
  esp_event_handler_instance_t ip_handler_ = nullptr;

  volatile microreader::WifiState state_ = microreader::WifiState::Off;
  bool hotspot_ = false;
  std::string ssid_;
  std::string ip_;
  std::string failure_;
  bool failed_auth_ = false;
  int attempts_ = 0;
  int auth_fails_ = 0;
  uint32_t deadline_ms_ = 0;

  // Set by the event task, consumed by poll().
  volatile bool ev_sta_start_ = false;
  volatile bool ev_got_ip_ = false;
  volatile bool ev_disconnected_ = false;
  volatile int disconnect_reason_ = 0;
  volatile uint32_t got_ip_ = 0;

  microreader::net::FileServer* server_ = nullptr;
  httpd_handle_t httpd_ = nullptr;
  bool servers_running_ = false;
  uint32_t server_ip_ = 0;
  volatile bool stopping_ = false;
  volatile bool busy_ = false;
  volatile bool dns_stop_ = false;
  volatile bool dns_running_ = false;
  uint8_t rx_[512];
  uint8_t tx_[512];

  SemaphoreHandle_t activity_mutex_ = nullptr;
  std::string activity_;
  volatile uint32_t activity_count_ = 0;
};
