#pragma once

#include <string>
#include <vector>

#include "ListMenuScreen.h"

namespace microreader {

// The Finished shelf (Settings → Finished Books): books marked as finished,
// newest first. Selecting one opens it; unmark it from the book's menu.
class FinishedBooksScreen final : public ListMenuScreen {
 public:
  const char* name() const override {
    return "Finished";
  }

  void stop() override {
    { std::vector<std::string> tmp; paths_.swap(tmp); }
    free_items_storage();
  }

 protected:
  void on_start() override;
  void on_select(int index) override;

 private:
  std::vector<std::string> paths_;  // per list item; empty for non-book rows
};

}  // namespace microreader
