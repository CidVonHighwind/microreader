#pragma once

#include <string>
#include <vector>

#include "mocks/WString.h"

namespace TestUtils {

// File I/O helpers
std::string readFile(const std::string& path);
std::string normalizeLineEndings(const std::string& s);

// Text comparison helpers
struct ComparisonResult {
  bool success;
  size_t firstDiffIndex;
  std::string expectedByte;
  std::string actualByte;
  size_t expectedLength;
  size_t actualLength;
};

ComparisonResult compareStrings(const std::string& expected, const std::string& actual);
void reportComparison(const ComparisonResult& result, const std::string& testName);

// Test result tracking
class TestRunner {
 public:
  TestRunner(const std::string& suiteName);
  ~TestRunner();

  void runTest(const std::string& testName, bool passed, const std::string& failureMessage = "");
  bool expectEqual(const std::string& expected, const std::string& actual, const std::string& testName,
                   bool verbose = false);
  bool expectTrue(bool condition, const std::string& testName, const std::string& message = "", bool silent = false);

  int getPassCount() const {
    return passCount_;
  }
  int getFailCount() const {
    return failCount_;
  }
  bool allPassed() const {
    return failCount_ == 0;
  }

  void printSummary() const;

 private:
  std::string suiteName_;
  int passCount_;
  int failCount_;
};

// Layout/word helper for tests
std::string joinWords(const std::vector<String>& words, const std::string& separator = " ");

}  // namespace TestUtils
