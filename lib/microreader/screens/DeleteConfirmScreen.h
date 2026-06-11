#pragma once

#include <string>

#include "../Input.h"
#include "../display/DrawBuffer.h"
#include "ListMenuScreen.h"

namespace microreader {

class DeleteConfirmScreen final : public ListMenuScreen {
 public:
  DeleteConfirmScreen() = default;

  void setup(const std::string& book_path) {
    book_path_ = book_path;
    // Extract filename from path
    const char* name = book_path_.c_str();
    const char* sep = std::strrchr(name, '/');
#ifdef _WIN32
    const char* bsep = std::strrchr(name, '\\');
    if (bsep && (!sep || bsep > sep))
      sep = bsep;
#endif
    if (sep)
      name = sep + 1;
    filename_ = name;
  }

  const char* name() const override {
    return "DeleteConfirm";
  }

 protected:
  void on_start() override;
  void on_select(int index) override;

 private:
  std::string book_path_;
  std::string filename_;
  int delete_idx_ = 0;
  int cancel_idx_ = 0;
  int filename_lines_ = 0;

  void delete_book_();
};

}  // namespace microreader
