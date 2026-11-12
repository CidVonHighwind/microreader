#include "FinishedBooksScreen.h"

#include <cstdio>

#include "../Application.h"

namespace microreader {

void FinishedBooksScreen::on_start() {
  title_ = "Finished Books";
  paths_.clear();

  const FinishedBooks& shelf = app_->finished_books();
  const size_t n = shelf.size();
  char sub[32];
  std::snprintf(sub, sizeof(sub), "%u book%s", static_cast<unsigned>(n), n == 1 ? "" : "s");
  subtitle_ = n ? sub : "";

  if (n == 0) {
    // Non-selectable hint rows.
    add_separator("No finished books yet.");
    add_separator("While reading, press \xe2\x96\xb6 and choose");
    add_separator("\"Finished: No\" to move a book here.");
    paths_.resize(3);
    return;
  }

  for (const auto& b : shelf.books()) {
    std::string label = b.title.empty() ? b.path : b.title;
    if (b.finished_at > 0) {
      const CivilDate d = date_from_local_time(b.finished_at);
      char date[24];
      std::snprintf(date, sizeof(date), "%d %s %d", d.day, month_abbrev(d.month), d.year);
      label = std::string(date) + "  \xc2\xb7  " + label;
    }
    add_item(label);
    paths_.push_back(b.path);
  }
}

void FinishedBooksScreen::on_select(int index) {
  if (index < 0 || index >= static_cast<int>(paths_.size()) || paths_[index].empty())
    return;
  // Don't call app_->record_book_opened() here: the book index isn't loaded
  // while the book list is paused, and saving it now would write it empty.
  const std::string path = paths_[index];
  app_->reader()->set_path(path);
  app_->push_screen(ScreenId::Reader);
}

}  // namespace microreader
