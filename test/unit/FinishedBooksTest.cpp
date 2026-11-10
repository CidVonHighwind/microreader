// Unit tests for lib/microreader/library/FinishedBooks.

#include "microreader/library/FinishedBooks.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace microreader;

namespace {

class FinishedBooksTest : public ::testing::Test {
 protected:
  fs::path dir_;
  std::string file_;

  void SetUp() override {
    dir_ = fs::temp_directory_path() /
           ("finished_books_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    fs::remove_all(dir_);
    fs::create_directories(dir_);
    file_ = (dir_ / "finished.txt").string();
  }
  void TearDown() override {
    fs::remove_all(dir_);
  }
};

}  // namespace

TEST_F(FinishedBooksTest, MarkUnmarkAndPersist) {
  FinishedBooks f;
  f.set_file(file_);
  f.load();
  EXPECT_EQ(f.size(), 0u);
  EXPECT_TRUE(f.set("/sdcard/books/a.epub", "Alpha", "Ann", true, 1000));
  EXPECT_TRUE(f.set("/sdcard/books/b.epub", "Beta", "", true, 2000));
  EXPECT_FALSE(f.set("/sdcard/books/a.epub", "Alpha", "Ann", true, 3000));  // already there
  EXPECT_TRUE(f.contains("/sdcard/books/a.epub", "", ""));
  ASSERT_EQ(f.size(), 2u);
  EXPECT_EQ(f.books()[0].title, "Beta");  // newest first
  EXPECT_EQ(f.books()[1].finished_at, 1000);

  FinishedBooks g;
  g.set_file(file_);
  g.load();
  ASSERT_EQ(g.size(), 2u);
  EXPECT_EQ(g.books()[1].path, "/sdcard/books/a.epub");
  EXPECT_EQ(g.books()[1].author, "Ann");
  EXPECT_EQ(g.books()[1].finished_at, 1000);

  EXPECT_TRUE(g.set("/sdcard/books/a.epub", "Alpha", "Ann", false, 0));
  EXPECT_FALSE(g.set("/sdcard/books/a.epub", "Alpha", "Ann", false, 0));
  FinishedBooks h;
  h.set_file(file_);
  h.load();
  ASSERT_EQ(h.size(), 1u);
  EXPECT_EQ(h.books()[0].title, "Beta");
}

TEST_F(FinishedBooksTest, MatchesByTitleAndAuthorAfterRename) {
  FinishedBooks f;
  f.set_file(file_);
  f.set("/sdcard/books/old name.epub", "Gamma", "Gus", true, 500);
  EXPECT_TRUE(f.contains("/sdcard/books/new name.epub", "Gamma", "Gus"));
  EXPECT_FALSE(f.contains("/sdcard/books/new name.epub", "Gamma", "Someone else"));
  EXPECT_FALSE(f.contains("/sdcard/books/other.epub", "", ""));
  // Re-marking under the new path updates the stored path but keeps the date.
  const uint64_t gen = f.generation();
  EXPECT_TRUE(f.set("/sdcard/books/new name.epub", "Gamma", "Gus", true, 900));
  EXPECT_GT(f.generation(), gen);
  EXPECT_EQ(f.books()[0].path, "/sdcard/books/new name.epub");
  EXPECT_EQ(f.books()[0].finished_at, 500);
}

TEST_F(FinishedBooksTest, TabsInFieldsDontBreakTheFile) {
  FinishedBooks f;
  f.set_file(file_);
  f.set("/sdcard/books/x.epub", "Title\twith\ttabs", "A\nB", true, 7);
  FinishedBooks g;
  g.set_file(file_);
  g.load();
  ASSERT_EQ(g.size(), 1u);
  EXPECT_EQ(g.books()[0].title, "Title with tabs");
  EXPECT_EQ(g.books()[0].author, "A B");
}

TEST_F(FinishedBooksTest, LoadSkipsBadLinesAndDuplicates) {
  std::ofstream(file_) << "# header\n"
                       << "garbage-without-tabs\n"
                       << "12\t\tNo path\tX\n"
                       << "34\t/a.epub\tA\tAuthor\r\n"
                       << "35\t/a.epub\tA\tAuthor\n"
                       << "notanumber\t/b.epub\n";
  FinishedBooks f;
  f.set_file(file_);
  f.load();
  ASSERT_EQ(f.size(), 2u);
  EXPECT_TRUE(f.contains("/a.epub", "", ""));
  EXPECT_TRUE(f.contains("/b.epub", "", ""));
  for (const auto& b : f.books())
    if (b.path == "/a.epub")
      EXPECT_EQ(b.author, "Author");  // CR stripped
}

TEST_F(FinishedBooksTest, NoFileMeansInMemoryOnly) {
  FinishedBooks f;
  f.load();
  EXPECT_TRUE(f.set("/a.epub", "A", "", true, 1));
  EXPECT_TRUE(f.contains("/a.epub", "", ""));
  EXPECT_FALSE(f.save());
}
