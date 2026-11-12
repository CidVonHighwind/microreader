// Unit tests for lib/microreader/library/BookProgress.

#include "microreader/library/BookProgress.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace microreader;

namespace {

class BookProgressTest : public ::testing::Test {
 protected:
  fs::path dir_;
  std::string file_;
  void SetUp() override {
    dir_ = fs::temp_directory_path() /
           ("book_progress_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    fs::remove_all(dir_);
    fs::create_directories(dir_);
    file_ = (dir_ / "progress.txt").string();
  }
  void TearDown() override {
    fs::remove_all(dir_);
  }
};

}  // namespace

TEST_F(BookProgressTest, SetGetSaveLoad) {
  BookProgress p;
  p.set_file(file_);
  p.load();
  EXPECT_EQ(p.get("/a.epub", "A", "x"), -1);
  p.set("/a.epub", "A", "x", 12);
  p.set("/b.epub", "B", "", 150);  // clamped
  EXPECT_EQ(p.get("/a.epub", "", ""), 12);
  EXPECT_EQ(p.get("/b.epub", "", ""), 100);
  EXPECT_TRUE(p.dirty());
  EXPECT_TRUE(p.save());
  EXPECT_FALSE(p.dirty());
  EXPECT_FALSE(p.save());  // nothing changed: no write

  BookProgress q;
  q.set_file(file_);
  q.load();
  EXPECT_EQ(q.size(), 2u);
  EXPECT_EQ(q.get("/a.epub", "", ""), 12);
  EXPECT_EQ(q.get("/moved/a.epub", "A", "x"), 12);  // title + author
  EXPECT_EQ(q.get("/moved/a.epub", "A", "y"), -1);
  q.set("/a.epub", "A", "x", 12);  // same value: not dirty
  EXPECT_FALSE(q.dirty());
  q.set("/moved/a.epub", "A", "x", 40);  // updates the entry and its path
  EXPECT_EQ(q.size(), 2u);
  EXPECT_EQ(q.get("/moved/a.epub", "", ""), 40);
}

TEST_F(BookProgressTest, BadLinesSkippedAndLaterLinesWin) {
  std::ofstream(file_) << "# header\n"
                       << "x\t/bad.epub\n"
                       << "5\t\n"
                       << "7\t/a.epub\tA\tAuth\r\n"
                       << "9\t/a.epub\tA\tAuth\n";
  BookProgress p;
  p.set_file(file_);
  p.load();
  EXPECT_EQ(p.size(), 1u);
  EXPECT_EQ(p.get("/a.epub", "", ""), 9);
}

TEST_F(BookProgressTest, KeepsAtMostMaxBooks) {
  BookProgress p;
  for (size_t i = 0; i < BookProgress::kMaxBooks + 3; ++i)
    p.set("/b" + std::to_string(i) + ".epub", "", "", 1);
  EXPECT_EQ(p.size(), BookProgress::kMaxBooks);
  EXPECT_EQ(p.get("/b0.epub", "", ""), -1);  // oldest forgotten
  EXPECT_EQ(p.get("/b" + std::to_string(BookProgress::kMaxBooks + 2) + ".epub", "", ""), 1);
}
