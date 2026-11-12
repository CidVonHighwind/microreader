#pragma once

// Tiny DNS message builders for Wi-Fi file transfer:
//  - an mDNS responder for "sami.local" (A records only, IPv4), and
//  - a "captive" DNS server for hotspot mode that answers every name with the
//    reader's address, so phones open the page by themselves.
//
// Pure functions over byte buffers; the ESP32 layer does the sockets.

#include <cstddef>
#include <cstdint>
#include <string>

namespace microreader {
namespace net {

constexpr uint16_t kMdnsPort = 5353;
constexpr uint8_t kMdnsGroup[4] = {224, 0, 0, 251};

// Reads a (possibly compressed) domain name starting at `offset`. On success
// stores it dotted ("sami.local") and sets `next` to the byte after the name
// in the original position. Returns false on malformed input.
bool dns_read_name(const uint8_t* msg, size_t len, size_t offset, std::string& name, size_t& next);

// If `query` is an mDNS query asking for `host` (e.g. "sami.local", case-
// insensitive) with type A or ANY, writes a response into `out` and returns
// its length; otherwise returns 0. `legacy` = the query came from a port other
// than 5353 (a plain DNS resolver): the reply then echoes the ID and question
// and goes back unicast. `*unicast` is set when the reply should go back to
// the sender rather than to the multicast group.
size_t mdns_answer(const uint8_t* query, size_t len, const char* host, const uint8_t ip[4], bool legacy,
                   uint8_t* out, size_t cap, bool* unicast);

// Unsolicited announcement of `host` → `ip` (sent when the server starts).
size_t mdns_announce(const char* host, const uint8_t ip[4], uint8_t* out, size_t cap);

// Answers a standard DNS query for any name with `ip` (A/ANY), or with an
// empty NOERROR answer for other types. Returns 0 for malformed input.
size_t dns_answer_all(const uint8_t* query, size_t len, const uint8_t ip[4], uint8_t* out, size_t cap);

}  // namespace net
}  // namespace microreader
