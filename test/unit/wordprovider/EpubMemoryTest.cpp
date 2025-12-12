/**
 * EpubMemoryTest.cpp - Memory Leak Detection Test for EpubWordProvider
 *
 * This test suite validates that EpubWordProvider does not leak memory
 * when navigating between chapters and reading words. It tracks memory
 * usage across multiple chapter transitions and word reads.
 *
 * Tests:
 * - Memory stability during chapter navigation
 * - Memory stability during word reading within a chapter
 * - Memory stability during repeated chapter cycling
 * - Memory stability when reading entire chapters
 */

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

// Platform-specific memory tracking
#ifdef _WIN32
// Prevent Windows.h from defining min/max macros that conflict with std::min/max
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
// windows.h must come before psapi.h
#include <windows.h>
#include <psapi.h>
// clang-format on
#pragma comment(lib, "psapi.lib")
#else
#include <sys/resource.h>
#endif

#include "content/providers/EpubWordProvider.h"
#include "test_config.h"
#include "test_globals.h"
#include "test_utils.h"

// Test configuration flags
namespace EpubMemoryTests {

constexpr bool TEST_CHAPTER_NAVIGATION_MEMORY = true;
constexpr bool TEST_WORD_READING_MEMORY = true;
constexpr bool TEST_CHAPTER_CYCLING_MEMORY = true;
constexpr bool TEST_FULL_CHAPTER_READ_MEMORY = true;

// How many cycles to run for each test
constexpr int CHAPTER_NAVIGATION_CYCLES = 5;
constexpr int CHAPTER_CYCLING_ITERATIONS = 100;
constexpr int WORDS_TO_READ_PER_CYCLE = 1000;

// Maximum allowed memory growth (in bytes) before flagging a potential leak
// This threshold accounts for some variance but catches significant leaks
constexpr size_t MAX_ALLOWED_MEMORY_GROWTH_BYTES = 1024 * 1024;  // 1MB

/**
 * Get current process memory usage in bytes
 */
size_t getCurrentMemoryUsage() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
    return pmc.WorkingSetSize;
  }
  return 0;
#else
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    // maxrss is in kilobytes on Linux, bytes on macOS
#ifdef __APPLE__
    return usage.ru_maxrss;
#else
    return usage.ru_maxrss * 1024;
#endif
  }
  return 0;
#endif
}

/**
 * Format bytes as human-readable string
 */
std::string formatBytes(size_t bytes) {
  const char* units[] = {"B", "KB", "MB", "GB"};
  int unitIndex = 0;
  double value = static_cast<double>(bytes);

  while (value >= 1024.0 && unitIndex < 3) {
    value /= 1024.0;
    unitIndex++;
  }

  char buffer[64];
  snprintf(buffer, sizeof(buffer), "%.2f %s", value, units[unitIndex]);
  return std::string(buffer);
}

/**
 * Structure to track memory samples
 */
struct MemorySample {
  std::string label;
  size_t bytes;
  int chapter;
  int iteration;
};

/**
 * Print memory report
 */
void printMemoryReport(const std::vector<MemorySample>& samples, const std::string& testName) {
  if (samples.empty())
    return;

  std::cout << "\n  Memory Report for " << testName << ":\n";
  std::cout << "  ----------------------------------------\n";

  size_t minMem = samples[0].bytes;
  size_t maxMem = samples[0].bytes;
  size_t startMem = samples[0].bytes;
  size_t endMem = samples.back().bytes;

  for (const auto& sample : samples) {
    minMem = std::min(minMem, sample.bytes);
    maxMem = std::max(maxMem, sample.bytes);
  }

  // Print first and last few samples
  size_t printCount = std::min(samples.size(), size_t(5));
  for (size_t i = 0; i < printCount; i++) {
    const auto& s = samples[i];
    std::cout << "    [" << s.label << "] Ch" << s.chapter << " Iter" << s.iteration << ": " << formatBytes(s.bytes)
              << "\n";
  }

  if (samples.size() > 10) {
    std::cout << "    ... (" << (samples.size() - 10) << " more samples) ...\n";
  }

  if (samples.size() > 5) {
    for (size_t i = samples.size() - 5; i < samples.size(); i++) {
      const auto& s = samples[i];
      std::cout << "    [" << s.label << "] Ch" << s.chapter << " Iter" << s.iteration << ": " << formatBytes(s.bytes)
                << "\n";
    }
  }

  std::cout << "  ----------------------------------------\n";
  std::cout << "  Start:  " << formatBytes(startMem) << "\n";
  std::cout << "  End:    " << formatBytes(endMem) << "\n";
  std::cout << "  Min:    " << formatBytes(minMem) << "\n";
  std::cout << "  Max:    " << formatBytes(maxMem) << "\n";

  long long growth = static_cast<long long>(endMem) - static_cast<long long>(startMem);
  std::cout << "  Growth: " << (growth >= 0 ? "+" : "") << formatBytes(std::abs(growth));
  if (growth > 0) {
    std::cout << " (potential leak if consistently growing)";
  }
  std::cout << "\n";
}

/**
 * Test: Memory stability during chapter navigation
 *
 * Opens chapters in sequence and monitors memory usage.
 * Memory should remain relatively stable after initial loading.
 */
void testChapterNavigationMemory(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Chapter Navigation Memory ===\n";

  // Create fresh provider for this test
  EpubWordProvider provider(TestGlobals::g_testFilePath);
  if (!provider.isValid()) {
    runner.expectTrue(false, "Provider should be valid for memory test");
    return;
  }

  int chapterCount = provider.getChapterCount();
  std::cout << "  EPUB has " << chapterCount << " chapters\n";

  if (chapterCount < 2) {
    std::cout << "  Skipping test: need at least 2 chapters\n";
    runner.expectTrue(true, "Skipped - need more chapters");
    return;
  }

  std::vector<MemorySample> samples;

  // Sample initial memory
  size_t initialMemory = getCurrentMemoryUsage();
  samples.push_back({"initial", initialMemory, 0, 0});

  // Navigate through chapters multiple times
  for (int cycle = 0; cycle < CHAPTER_NAVIGATION_CYCLES; cycle++) {
    for (int ch = 0; ch < std::min(chapterCount, 10); ch++) {  // Cap at 10 chapters per cycle
      provider.setChapter(ch);

      // Read a few words to ensure chapter is actually loaded
      for (int w = 0; w < 100 && provider.hasNextWord(); w++) {
        provider.getNextWord();
      }

      size_t memNow = getCurrentMemoryUsage();
      samples.push_back({"nav", memNow, ch, cycle});
    }
  }

  // Sample final memory
  size_t finalMemory = getCurrentMemoryUsage();
  samples.push_back({"final", finalMemory, 0, CHAPTER_NAVIGATION_CYCLES});

  printMemoryReport(samples, "Chapter Navigation");

  // Check for memory growth
  long long growth = static_cast<long long>(finalMemory) - static_cast<long long>(initialMemory);
  bool memoryStable = growth < static_cast<long long>(MAX_ALLOWED_MEMORY_GROWTH_BYTES);

  runner.expectTrue(memoryStable, "Memory should not grow significantly during chapter navigation (growth: " +
                                      formatBytes(std::abs(growth)) + ")");
}

/**
 * Test: Memory stability during word reading
 *
 * Reads many words from a single chapter and monitors memory usage.
 */
void testWordReadingMemory(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Word Reading Memory ===\n";

  EpubWordProvider provider(TestGlobals::g_testFilePath);
  if (!provider.isValid()) {
    runner.expectTrue(false, "Provider should be valid for memory test");
    return;
  }

  // Set to first chapter with content
  provider.setChapter(0);

  std::vector<MemorySample> samples;
  size_t initialMemory = getCurrentMemoryUsage();
  samples.push_back({"initial", initialMemory, 0, 0});

  int totalWordsRead = 0;
  const int sampleInterval = 500;

  // Read words in chunks, sampling memory periodically
  while (provider.hasNextWord() && totalWordsRead < WORDS_TO_READ_PER_CYCLE * 10) {
    String word = provider.getNextWord().text;
    if (word.length() == 0)
      break;

    totalWordsRead++;

    if (totalWordsRead % sampleInterval == 0) {
      size_t memNow = getCurrentMemoryUsage();
      samples.push_back({"read", memNow, 0, totalWordsRead / sampleInterval});
    }
  }

  size_t finalMemory = getCurrentMemoryUsage();
  samples.push_back({"final", finalMemory, 0, totalWordsRead});

  std::cout << "  Read " << totalWordsRead << " words\n";
  printMemoryReport(samples, "Word Reading");

  long long growth = static_cast<long long>(finalMemory) - static_cast<long long>(initialMemory);
  bool memoryStable = growth < static_cast<long long>(MAX_ALLOWED_MEMORY_GROWTH_BYTES);

  runner.expectTrue(memoryStable, "Memory should not grow significantly during word reading (growth: " +
                                      formatBytes(std::abs(growth)) + ")");
}

/**
 * Test: Memory stability during repeated chapter cycling
 *
 * Repeatedly switches between the same chapters to detect
 * memory leaks from chapter loading/unloading.
 */
void testChapterCyclingMemory(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Chapter Cycling Memory ===\n";

  EpubWordProvider provider(TestGlobals::g_testFilePath);
  if (!provider.isValid()) {
    runner.expectTrue(false, "Provider should be valid for memory test");
    return;
  }

  int chapterCount = provider.getChapterCount();
  if (chapterCount < 2) {
    std::cout << "  Skipping test: need at least 2 chapters\n";
    runner.expectTrue(true, "Skipped - need more chapters");
    return;
  }

  std::vector<MemorySample> samples;
  size_t initialMemory = getCurrentMemoryUsage();
  samples.push_back({"initial", initialMemory, 0, 0});

  // Warm up - load first two chapters once
  provider.setChapter(0);
  provider.setChapter(1);

  size_t warmupMemory = getCurrentMemoryUsage();
  samples.push_back({"warmup", warmupMemory, 1, 0});

  // Now cycle between chapters many times
  for (int i = 0; i < CHAPTER_CYCLING_ITERATIONS; i++) {
    // Cycle through first few chapters
    for (int ch = 0; ch < std::min(chapterCount, 5); ch++) {
      provider.setChapter(ch);

      // Read some words to ensure content is loaded
      for (int w = 0; w < 50 && provider.hasNextWord(); w++) {
        provider.getNextWord();
      }
    }

    size_t memNow = getCurrentMemoryUsage();
    samples.push_back({"cycle", memNow, 0, i});
  }

  size_t finalMemory = getCurrentMemoryUsage();
  samples.push_back({"final", finalMemory, 0, CHAPTER_CYCLING_ITERATIONS});

  printMemoryReport(samples, "Chapter Cycling");

  // Memory growth should be minimal after warmup
  long long growthFromWarmup = static_cast<long long>(finalMemory) - static_cast<long long>(warmupMemory);
  bool memoryStable = growthFromWarmup < static_cast<long long>(MAX_ALLOWED_MEMORY_GROWTH_BYTES / 2);

  runner.expectTrue(memoryStable, "Memory should not grow after warmup during cycling (growth: " +
                                      formatBytes(std::abs(growthFromWarmup)) + ")");
}

/**
 * Test: Memory stability when reading entire chapters
 *
 * Reads entire chapters to completion and checks for leaks.
 */
void testFullChapterReadMemory(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Full Chapter Read Memory ===\n";

  EpubWordProvider provider(TestGlobals::g_testFilePath);
  if (!provider.isValid()) {
    runner.expectTrue(false, "Provider should be valid for memory test");
    return;
  }

  int chapterCount = provider.getChapterCount();
  int chaptersToRead = std::min(chapterCount, 5);  // Read up to 5 chapters

  std::vector<MemorySample> samples;
  size_t initialMemory = getCurrentMemoryUsage();
  samples.push_back({"initial", initialMemory, 0, 0});

  int totalWordsRead = 0;

  for (int ch = 0; ch < chaptersToRead; ch++) {
    provider.setChapter(ch);

    int chapterWords = 0;
    while (provider.hasNextWord()) {
      String word = provider.getNextWord().text;
      if (word.length() == 0)
        break;
      chapterWords++;
      totalWordsRead++;
    }

    size_t memNow = getCurrentMemoryUsage();
    samples.push_back({"chapter_done", memNow, ch, chapterWords});

    std::cout << "  Chapter " << ch << ": " << chapterWords << " words\n";
  }

  size_t finalMemory = getCurrentMemoryUsage();
  samples.push_back({"final", finalMemory, chaptersToRead, totalWordsRead});

  std::cout << "  Total words read: " << totalWordsRead << "\n";
  printMemoryReport(samples, "Full Chapter Read");

  long long growth = static_cast<long long>(finalMemory) - static_cast<long long>(initialMemory);
  bool memoryStable = growth < static_cast<long long>(MAX_ALLOWED_MEMORY_GROWTH_BYTES);

  runner.expectTrue(memoryStable, "Memory should not grow significantly when reading full chapters (growth: " +
                                      formatBytes(std::abs(growth)) + ")");
}

}  // namespace EpubMemoryTests

// ============================================================================
// Main entry point
// ============================================================================
int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;

  std::cout << "\n========================================\n";
  std::cout << "EpubWordProvider Memory Test Suite\n";
  std::cout << "========================================\n";
  std::cout << "EPUB file: " << TestGlobals::g_testFilePath << "\n";

  // Report initial memory
  size_t startMemory = EpubMemoryTests::getCurrentMemoryUsage();
  std::cout << "Initial process memory: " << EpubMemoryTests::formatBytes(startMemory) << "\n";

  TestUtils::TestRunner runner("EpubWordProvider Memory Tests");

  // Run memory tests
  if (EpubMemoryTests::TEST_CHAPTER_NAVIGATION_MEMORY) {
    EpubMemoryTests::testChapterNavigationMemory(runner);
  }

  if (EpubMemoryTests::TEST_WORD_READING_MEMORY) {
    EpubMemoryTests::testWordReadingMemory(runner);
  }

  if (EpubMemoryTests::TEST_CHAPTER_CYCLING_MEMORY) {
    EpubMemoryTests::testChapterCyclingMemory(runner);
  }

  if (EpubMemoryTests::TEST_FULL_CHAPTER_READ_MEMORY) {
    EpubMemoryTests::testFullChapterReadMemory(runner);
  }

  // Report final memory
  size_t endMemory = EpubMemoryTests::getCurrentMemoryUsage();
  std::cout << "\n========================================\n";
  std::cout << "Final process memory: " << EpubMemoryTests::formatBytes(endMemory) << "\n";
  long long totalGrowth = static_cast<long long>(endMemory) - static_cast<long long>(startMemory);
  std::cout << "Total memory change: " << (totalGrowth >= 0 ? "+" : "")
            << EpubMemoryTests::formatBytes(std::abs(totalGrowth)) << "\n";

  // Print summary
  runner.printSummary();

  return runner.allPassed() ? 0 : 1;
}
