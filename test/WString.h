// Mock WString.h for Windows builds
#ifndef WString_h
#define WString_h

#ifndef ARDUINO

#include <cstddef>
#include <string>

// Mock Arduino String class for Windows testing
class String {
 private:
  std::string data_;

 public:
  String() = default;
  String(const char* str) : data_(str ? str : "") {}
  String(const std::string& str) : data_(str) {}

  size_t length() const {
    return data_.size();
  }

  char operator[](size_t idx) const {
    return data_[idx];
  }

  const char* c_str() const {
    return data_.c_str();
  }

  String substring(size_t start, size_t end) const {
    if (start >= data_.size())
      return String("");
    if (end > data_.size())
      end = data_.size();
    return String(data_.substr(start, end - start));
  }

  void trim() {
    size_t start = data_.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
      data_.clear();
      return;
    }
    size_t end = data_.find_last_not_of(" \t\n\r");
    data_ = data_.substr(start, end - start + 1);
  }

  bool operator==(const String& other) const {
    return data_ == other.data_;
  }

  bool operator!=(const String& other) const {
    return data_ != other.data_;
  }
};

#endif  // ARDUINO

#endif  // WString_h
