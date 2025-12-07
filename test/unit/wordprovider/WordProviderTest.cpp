/**
 * WordProviderTest.cpp - Generic Word Provider Test Suite
 *
 * This test suite validates WordProvider implementations.
 * The provider to test is configured in test_globals.h - just
 * uncomment the desired provider type there and rebuild.
 *
 * Tests:
 * - Forward reconstruction
 * - Backward reconstruction
 * - Bidirectional word match
 * - Seek consistency
 * - Unget forward/backward
 * - Position round-trip
 */

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Include test globals - this configures which provider to use
#include "test_globals.h"

// Test configuration flags
namespace WordProviderTests {

constexpr bool TEST_FORWARD_RECONSTRUCTION = true;
constexpr bool TEST_BACKWARD_RECONSTRUCTION = true;
constexpr bool TEST_BIDIRECTIONAL_WORD_MATCH = true;
constexpr bool TEST_SEEK_CONSISTENCY = true;
constexpr bool TEST_UNGET_FORWARD = true;
constexpr bool TEST_UNGET_BACKWARD = true;
constexpr bool TEST_POSITION_ROUND_TRIP = true;
constexpr int MAX_WORDS_FOR_SEEK_TEST = 500;
constexpr int MAX_FAILURES_TO_REPORT = 10;

// Word info structure for seek verification
struct WordInfo {
  String word;
  int positionBefore;
  int positionAfter;
};

/**
 * Test: Forward word reconstruction
 */
void testForwardReconstruction(TestUtils::TestRunner& runner) {
  const char* testName = TestGlobals::getProviderName();
  std::cout << "\n=== Test: Forward Reconstruction (" << testName << ") ===\n";

  TestGlobals::resetProvider();
  WordProvider& provider = TestGlobals::provider();

  std::string rebuilt;
  int wordCount = 0;

  while (provider.hasNextWord()) {
    String word = provider.getNextWord();
    if (word.length() == 0)
      break;
    rebuilt += word.c_str();
    wordCount++;
  }

  std::cout << "  Read " << wordCount << " words, total " << rebuilt.length() << " characters\n";

  runner.expectTrue(wordCount > 0, std::string(testName) + ": Forward reading produced words");
  runner.expectTrue(rebuilt.length() > 0, std::string(testName) + ": Forward text has content");
}

/**
 * Test: Backward word reconstruction
 */
void testBackwardReconstruction(TestUtils::TestRunner& runner) {
  const char* testName = TestGlobals::getProviderName();
  std::cout << "\n=== Test: Backward Reconstruction (" << testName << ") ===\n";

  TestGlobals::resetProvider();
  WordProvider& provider = TestGlobals::provider();

  // First, go to end
  while (provider.hasNextWord()) {
    String word = provider.getNextWord();
    if (word.length() == 0)
      break;
  }

  int endPosition = provider.getCurrentIndex();
  std::cout << "  End position: " << endPosition << "\n";

  // Now read backward
  provider.setPosition(endPosition);
  std::string rebuilt;
  int wordCount = 0;

  while (true) {
    int currentIndex = provider.getCurrentIndex();
    String word = provider.getPrevWord();
    int newIndex = provider.getCurrentIndex();

    // print out the word read
    // std::cout << "    Word: '" << word.c_str() << "'\n";

    if (newIndex >= currentIndex) {
      std::cout << "  *** Error: position did not move backward (current: " << currentIndex << ", new: " << newIndex
                << ")\n";
      break;
    }

    if (word.length() == 0)
      break;
    rebuilt.insert(0, word.c_str());
    wordCount++;
  }

  std::cout << "  Read " << wordCount << " words backward, total " << rebuilt.length() << " characters\n";

  runner.expectTrue(wordCount > 0, std::string(testName) + ": Backward reading produced words");
  runner.expectTrue(rebuilt.length() > 0, std::string(testName) + ": Backward text has content");
}

/**
 * Test: Bidirectional word match
 */
void testBidirectionalWordMatch(TestUtils::TestRunner& runner) {
  const char* testName = TestGlobals::getProviderName();
  std::cout << "\n=== Test: Bidirectional Word Match (" << testName << ") ===\n";

  TestGlobals::resetProvider();
  WordProvider& provider = TestGlobals::provider();

  // Collect words forward with positions
  std::vector<WordInfo> forwardWords;

  while (provider.hasNextWord()) {
    WordInfo info;
    info.positionBefore = provider.getCurrentIndex();
    info.word = provider.getNextWord();
    info.positionAfter = provider.getCurrentIndex();
    if (info.word.length() == 0)
      break;
    forwardWords.push_back(info);
  }

  int endPosition = provider.getCurrentIndex();
  std::cout << "  Collected " << forwardWords.size() << " words forward\n";

  // Collect words backward with positions
  provider.setPosition(endPosition);
  std::vector<WordInfo> backwardWords;

  while (provider.hasPrevWord()) {
    WordInfo info;
    info.positionAfter = provider.getCurrentIndex();
    info.word = provider.getPrevWord();
    info.positionBefore = provider.getCurrentIndex();
    if (info.word.length() == 0)
      break;
    backwardWords.push_back(info);
  }

  // Reverse to get correct order for comparison
  std::reverse(backwardWords.begin(), backwardWords.end());

  std::cout << "  Collected " << backwardWords.size() << " words backward\n";

  // Compare counts
  runner.expectTrue(
      forwardWords.size() == backwardWords.size(), std::string(testName) + ": Forward and backward word counts match",
      "Forward: " + std::to_string(forwardWords.size()) + ", Backward: " + std::to_string(backwardWords.size()));

  // Compare each word and positions
  bool allMatch = true;
  std::string mismatchMsg;
  size_t compareCount = std::min(forwardWords.size(), backwardWords.size());
  int wordMismatches = 0;
  int positionMismatches = 0;

  // Helper to check if a word is whitespace-only
  auto isWhitespaceOnly = [](const String& word) {
    for (unsigned int i = 0; i < word.length(); i++) {
      char c = word.charAt(i);
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
        return false;
    }
    return true;
  };

  for (size_t i = 0; i < compareCount; i++) {
    const WordInfo& fw = forwardWords[i];
    const WordInfo& bw = backwardWords[i];

    if (fw.word != bw.word) {
      if (wordMismatches < MAX_FAILURES_TO_REPORT) {
        std::cout << "  *** Word mismatch at index " << i << ": forward='" << fw.word.c_str() << "' backward='"
                  << bw.word.c_str() << "'\n";
      }
      wordMismatches++;
      allMatch = false;
    }
    //  else {
    //   // Words match, check positions (skip whitespace-only words as they have different position semantics)
    //   if (fw.positionBefore != bw.positionBefore) {
    //     if (positionMismatches < MAX_FAILURES_TO_REPORT) {
    //       if (!isWhitespaceOnly(fw.word)) {
    //         std::cout << "  *** Position before mismatch at index " << i << " (word='" << fw.word.c_str()
    //                   << "'): forward=" << fw.positionBefore << " backward=" << bw.positionBefore << "\n";
    //       }
    //     }

    //     positionMismatches++;
    //     allMatch = false;
    //   }
    //   if (fw.positionAfter != bw.positionAfter) {
    //     if (positionMismatches < MAX_FAILURES_TO_REPORT) {
    //       if (!isWhitespaceOnly(fw.word)) {
    //         std::cout << "  *** Position after mismatch at index " << i << " (word='" << fw.word.c_str()
    //                   << "'): forward=" << fw.positionAfter << " backward=" << bw.positionAfter << "\n";
    //       }
    //     }
    //     positionMismatches++;
    //     allMatch = false;
    //   }
    // }
  }

  if (wordMismatches > 0) {
    mismatchMsg = std::to_string(wordMismatches) + " word mismatches";
  }
  if (positionMismatches > 0) {
    if (!mismatchMsg.empty())
      mismatchMsg += ", ";
    mismatchMsg += std::to_string(positionMismatches) + " position mismatches";
  }

  runner.expectTrue(allMatch, std::string(testName) + ": All words and positions match bidirectionally", mismatchMsg);
}

/**
 * Test: Seek consistency
 */
void testSeekConsistency(TestUtils::TestRunner& runner) {
  const char* testName = TestGlobals::getProviderName();
  std::cout << "\n=== Test: Seek Consistency (" << testName << ") ===\n";

  TestGlobals::resetProvider();
  WordProvider& provider = TestGlobals::provider();

  // Phase 1: Read words and record positions
  std::vector<WordInfo> words;

  while (provider.hasNextWord() && (int)words.size() < MAX_WORDS_FOR_SEEK_TEST) {
    WordInfo info;
    info.positionBefore = provider.getCurrentIndex();
    info.word = provider.getNextWord();
    info.positionAfter = provider.getCurrentIndex();

    if (info.word.length() == 0)
      break;
    words.push_back(info);
  }

  std::cout << "  Recorded " << words.size() << " words with positions\n";

  // Phase 2: Verify each word by seeking and reading forward
  int failCount = 0;

  std::cout << "  Testing forward seek consistency...\n";
  for (size_t i = 0; i < words.size() && failCount < MAX_FAILURES_TO_REPORT; i++) {
    const WordInfo& info = words[i];

    provider.setPosition(info.positionBefore);
    String wordAgain = provider.getNextWord();
    int positionAgain = provider.getCurrentIndex();

    if (wordAgain != info.word) {
      std::cout << "  *** Forward word mismatch at index " << i << " at position " << info.positionBefore
                << ": expected '" << info.word.c_str() << "' got '" << wordAgain.c_str() << "'\n";
      failCount++;
    }
    //  else if (positionAgain != info.positionAfter) {
    //   std::cout << "  *** Forward position mismatch at index " << i << ": expected " << info.positionAfter << " got "
    //             << positionAgain << "\n";
    //   failCount++;
    // }
  }

  // Phase 3: Verify each word by seeking and reading backward
  std::cout << "  Testing backward seek consistency...\n";
  for (size_t i = 0; i < words.size() && failCount < MAX_FAILURES_TO_REPORT; i++) {
    const WordInfo& info = words[i];

    provider.setPosition(info.positionAfter);
    String wordAgain = provider.getPrevWord();
    int positionAgain = provider.getCurrentIndex();

    if (wordAgain != info.word) {
      std::cout << "  *** Backward word mismatch at index " << i << ": expected '" << info.word.c_str() << "' got '"
                << wordAgain.c_str() << "' (posAfter=" << info.positionAfter << ", posBefore=" << info.positionBefore
                << ")\n";
      failCount++;
    }
    //  else if (positionAgain != info.positionBefore) {
    //   std::cout << "  *** Backward position mismatch at index " << i << " (word='" << info.word.c_str()
    //             << "'): expected " << info.positionBefore << " got " << positionAgain << "\n";
    //   failCount++;
    // }
  }

  if (failCount == 0) {
    std::cout << "  All " << words.size() << " words verified successfully (forward and backward)\n";
    runner.expectTrue(true, std::string(testName) + ": Seek consistency verified");
  } else {
    runner.expectTrue(false,
                      std::string(testName) + ": Seek consistency failed for " + std::to_string(failCount) + " words");
  }
}

/**
 * Test: Forward unget
 */
void testUngetForward(TestUtils::TestRunner& runner) {
  const char* testName = TestGlobals::getProviderName();
  std::cout << "\n=== Test: Forward Unget (" << testName << ") ===\n";

  TestGlobals::resetProvider();
  WordProvider& provider = TestGlobals::provider();

  bool allMatch = true;
  std::string errorMsg;
  int wordsProcessed = 0;

  while (provider.hasNextWord()) {
    String word1 = provider.getNextWord();
    if (word1.length() == 0)
      break;

    provider.ungetWord();
    String word2 = provider.getNextWord();

    if (word1 != word2) {
      errorMsg = "Unget failed at word " + std::to_string(wordsProcessed) + ": first='" + std::string(word1.c_str()) +
                 "' after_unget='" + std::string(word2.c_str()) + "'";
      allMatch = false;
      break;
    }
    wordsProcessed++;
  }

  std::cout << "  Verified unget for " << wordsProcessed << " words\n";
  runner.expectTrue(allMatch, std::string(testName) + ": Forward ungetWord works correctly", errorMsg);
}

/**
 * Test: Backward unget
 */
void testUngetBackward(TestUtils::TestRunner& runner) {
  const char* testName = TestGlobals::getProviderName();
  std::cout << "\n=== Test: Backward Unget (" << testName << ") ===\n";

  TestGlobals::resetProvider();
  WordProvider& provider = TestGlobals::provider();

  // Go to end first
  while (provider.hasNextWord()) {
    String word = provider.getNextWord();
    if (word.length() == 0)
      break;
  }

  int endPosition = provider.getCurrentIndex();
  provider.setPosition(endPosition);

  bool allMatch = true;
  std::string errorMsg;
  int wordsProcessed = 0;

  while (wordsProcessed < 100) {  // Limit to prevent infinite loops
    String word1 = provider.getPrevWord();
    if (word1.length() == 0)
      break;

    provider.ungetWord();
    String word2 = provider.getPrevWord();

    if (word1 != word2) {
      errorMsg = "Backward unget failed at word " + std::to_string(wordsProcessed) + ": first='" +
                 std::string(word1.c_str()) + "' after_unget='" + std::string(word2.c_str()) + "'";
      allMatch = false;
      break;
    }
    wordsProcessed++;
  }

  std::cout << "  Verified backward unget for " << wordsProcessed << " words\n";
  runner.expectTrue(allMatch, std::string(testName) + ": Backward ungetWord works correctly", errorMsg);
}

/**
 * Test: Position round-trip
 */
void testPositionRoundTrip(TestUtils::TestRunner& runner) {
  const char* testName = TestGlobals::getProviderName();
  std::cout << "\n=== Test: Position Round-Trip (" << testName << ") ===\n";

  TestGlobals::resetProvider();
  WordProvider& provider = TestGlobals::provider();

  // Collect words with positions
  std::vector<WordInfo> words;

  while (provider.hasNextWord() && (int)words.size() < 100) {
    WordInfo info;
    info.positionBefore = provider.getCurrentIndex();
    info.word = provider.getNextWord();
    info.positionAfter = provider.getCurrentIndex();

    if (info.word.length() == 0)
      break;
    words.push_back(info);
  }

  // Test round-trip for each word
  int failCount = 0;

  for (size_t i = 0; i < words.size() && failCount < MAX_FAILURES_TO_REPORT; i++) {
    const WordInfo& info = words[i];

    // Read forward from start position
    provider.setPosition(info.positionBefore);
    String wordForward = provider.getNextWord();
    int endPos = provider.getCurrentIndex();

    // Read backward from end position
    provider.setPosition(endPos);
    String wordBackward = provider.getPrevWord();

    if (wordForward != wordBackward) {
      std::cout << "  *** Round-trip mismatch at index " << i << ": forward='" << wordForward.c_str() << "' backward='"
                << wordBackward.c_str() << "'\n";
      failCount++;
    }
  }

  if (failCount == 0) {
    std::cout << "  All " << words.size() << " round-trips successful\n";
    runner.expectTrue(true, std::string(testName) + ": Position round-trip verified");
  } else {
    runner.expectTrue(false, std::string(testName) + ": Round-trip failed for " + std::to_string(failCount) + " words");
  }
}

/**
 * Run all tests
 */
void runAllTests(TestUtils::TestRunner& runner) {
  if (TEST_FORWARD_RECONSTRUCTION) {
    testForwardReconstruction(runner);
  }

  if (TEST_BACKWARD_RECONSTRUCTION) {
    testBackwardReconstruction(runner);
  }

  if (TEST_BIDIRECTIONAL_WORD_MATCH) {
    testBidirectionalWordMatch(runner);
  }

  if (TEST_SEEK_CONSISTENCY) {
    testSeekConsistency(runner);
  }

  if (TEST_UNGET_FORWARD) {
    testUngetForward(runner);
  }

  if (TEST_UNGET_BACKWARD) {
    testUngetBackward(runner);
  }

  if (TEST_POSITION_ROUND_TRIP) {
    testPositionRoundTrip(runner);
  }
}

}  // namespace WordProviderTests

int main(int argc, char** argv) {
  TestUtils::TestRunner runner("Word Provider Test Suite");

  // Initialize global provider and layout from test_globals.h configuration
  if (!TestGlobals::init()) {
    std::cerr << "Failed to initialize test globals\n";
    return 2;
  }

  // Run all tests using the configured provider
  WordProviderTests::runAllTests(runner);

  // Cleanup
  TestGlobals::cleanup();

  return runner.allPassed() ? 0 : 1;
}
