#include "Dns.h"

#include <cctype>
#include <cstring>

namespace microreader {
namespace net {

namespace {

constexpr uint16_t kTypeA = 1;
constexpr uint16_t kTypeAny = 255;
constexpr uint16_t kClassIn = 1;

uint16_t rd16(const uint8_t* p) {
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

struct Writer {
  uint8_t* out;
  size_t cap;
  size_t n = 0;
  bool ok = true;
  void u8(uint8_t v) {
    if (n < cap)
      out[n++] = v;
    else
      ok = false;
  }
  void u16(uint16_t v) {
    u8(static_cast<uint8_t>(v >> 8));
    u8(static_cast<uint8_t>(v));
  }
  void u32(uint32_t v) {
    u16(static_cast<uint16_t>(v >> 16));
    u16(static_cast<uint16_t>(v));
  }
  void bytes(const uint8_t* p, size_t len) {
    for (size_t i = 0; i < len; ++i)
      u8(p[i]);
  }
  // Dotted name → DNS labels.
  void name(const char* dotted) {
    const char* p = dotted;
    while (*p) {
      const char* dot = std::strchr(p, '.');
      const size_t l = dot ? static_cast<size_t>(dot - p) : std::strlen(p);
      if (l == 0 || l > 63) {
        ok = false;
        return;
      }
      u8(static_cast<uint8_t>(l));
      bytes(reinterpret_cast<const uint8_t*>(p), l);
      p += l;
      if (*p == '.')
        ++p;
    }
    u8(0);
  }
};

bool iequals(const std::string& a, const char* b) {
  const size_t n = std::strlen(b);
  if (a.size() != n)
    return false;
  for (size_t i = 0; i < n; ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  return true;
}

}  // namespace

bool dns_read_name(const uint8_t* msg, size_t len, size_t offset, std::string& name, size_t& next) {
  name.clear();
  size_t pos = offset;
  bool jumped = false;
  int jumps = 0;
  while (true) {
    if (pos >= len)
      return false;
    const uint8_t l = msg[pos];
    if (l == 0) {
      if (!jumped)
        next = pos + 1;
      return true;
    }
    if ((l & 0xC0) == 0xC0) {  // compression pointer
      if (pos + 1 >= len || ++jumps > 16)
        return false;
      if (!jumped)
        next = pos + 2;
      jumped = true;
      pos = static_cast<size_t>(((l & 0x3F) << 8) | msg[pos + 1]);
      continue;
    }
    if ((l & 0xC0) != 0 || pos + 1 + l > len)
      return false;
    if (!name.empty())
      name.push_back('.');
    name.append(reinterpret_cast<const char*>(msg + pos + 1), l);
    if (name.size() > 255)
      return false;
    pos += 1 + l;
  }
}

size_t mdns_answer(const uint8_t* query, size_t len, const char* host, const uint8_t ip[4], bool legacy,
                   uint8_t* out, size_t cap, bool* unicast) {
  if (unicast)
    *unicast = false;
  if (len < 12)
    return 0;
  const uint16_t flags = rd16(query + 2);
  if (flags & 0x8000)  // a response, not a query
    return 0;
  const uint16_t qdcount = rd16(query + 4);
  size_t pos = 12;
  bool match = false;
  bool qu = false;
  for (uint16_t i = 0; i < qdcount && i < 32; ++i) {
    std::string name;
    size_t next = 0;
    if (!dns_read_name(query, len, pos, name, next) || next + 4 > len)
      return 0;
    const uint16_t qtype = rd16(query + next);
    const uint16_t qclass = rd16(query + next + 2);
    pos = next + 4;
    if (!match && iequals(name, host) && (qtype == kTypeA || qtype == kTypeAny) &&
        (qclass & 0x7FFF) == kClassIn) {
      match = true;
      qu = (qclass & 0x8000) != 0;
    }
  }
  if (!match)
    return 0;

  Writer w{out, cap};
  w.u16(legacy ? rd16(query) : 0);  // ID: echoed only for legacy resolvers
  w.u16(0x8400);                    // response, authoritative
  w.u16(legacy ? 1 : 0);            // QDCOUNT
  w.u16(1);                         // ANCOUNT
  w.u16(0);
  w.u16(0);
  if (legacy) {
    // Re-encode the question uncompressed (its name may point elsewhere in the query).
    w.name(host);
    w.u16(kTypeA);
    w.u16(kClassIn);
  }
  w.name(host);
  w.u16(kTypeA);
  w.u16(legacy ? kClassIn : static_cast<uint16_t>(0x8000 | kClassIn));  // cache-flush for mDNS
  w.u32(legacy ? 10 : 120);                                             // TTL
  w.u16(4);
  w.bytes(ip, 4);
  if (!w.ok)
    return 0;
  if (unicast)
    *unicast = legacy || qu;
  return w.n;
}

size_t mdns_announce(const char* host, const uint8_t ip[4], uint8_t* out, size_t cap) {
  Writer w{out, cap};
  w.u16(0);
  w.u16(0x8400);
  w.u16(0);
  w.u16(1);
  w.u16(0);
  w.u16(0);
  w.name(host);
  w.u16(kTypeA);
  w.u16(static_cast<uint16_t>(0x8000 | kClassIn));
  w.u32(120);
  w.u16(4);
  w.bytes(ip, 4);
  return w.ok ? w.n : 0;
}

size_t dns_answer_all(const uint8_t* query, size_t len, const uint8_t ip[4], uint8_t* out, size_t cap) {
  if (len < 12)
    return 0;
  const uint16_t flags = rd16(query + 2);
  if ((flags & 0x8000) || rd16(query + 4) < 1)
    return 0;
  std::string name;
  size_t next = 0;
  if (!dns_read_name(query, len, 12, name, next) || next + 4 > len)
    return 0;
  const uint16_t qtype = rd16(query + next);
  const size_t q_end = next + 4;
  const bool answer = qtype == kTypeA || qtype == kTypeAny;

  Writer w{out, cap};
  w.u16(rd16(query));                                    // same ID
  // Response, authoritative, recursion available; copy the opcode and RD bit.
  w.u16(static_cast<uint16_t>(0x8480 | (flags & 0x0100) | (flags & 0x7800)));
  w.u16(1);
  w.u16(answer ? 1 : 0);
  w.u16(0);
  w.u16(0);
  w.bytes(query + 12, q_end - 12);  // the question, as asked
  if (answer) {
    w.u16(0xC00C);  // name: pointer to the question
    w.u16(kTypeA);
    w.u16(kClassIn);
    w.u32(60);
    w.u16(4);
    w.bytes(ip, 4);
  }
  return w.ok ? w.n : 0;
}

}  // namespace net
}  // namespace microreader
