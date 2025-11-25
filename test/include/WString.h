// Minimal WString.h for host tests. Provides a lightweight `String` used
// by text layout and StringWordProvider. This is intentionally small and
// used only for building unit tests on the host.
#pragma once

#include <string>

class String {
 public:
  String() = default;
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(const String& other) = default;

  int length() const {
    return static_cast<int>(s_.size());
  }
  const char* c_str() const {
    return s_.c_str();
  }
  char operator[](size_t i) const {
    return s_[i];
  }
  bool operator==(const char* rhs) const {
    if (!rhs)
      return s_.empty();
    return s_ == std::string(rhs);
  }
  bool operator==(const String& rhs) const {
    return s_ == rhs.s_;
  }
  bool operator!=(const char* rhs) const {
    return !(*this == rhs);
  }
  bool operator!=(const String& rhs) const {
    return !(*this == rhs);
  }
  String substring(int start, int end) const {
    if (start < 0)
      start = 0;
    if (end < start)
      end = start;
    if (end > (int)s_.size())
      end = (int)s_.size();
    return String(s_.substr(start, end - start));
  }

 private:
  std::string s_;
};
