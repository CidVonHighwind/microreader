/**
 * WordProviderSeekTest.cpp - Word Provider Seek/Position Test Suite
 *
 * This test suite thoroughly validates seeking and position handling.
 * The provider to test is configured in test_globals.h - just
 * uncomment the desired provider type there and rebuild.
 *
 * Tests:
 * - Forward seek consistency
 * - Backward seek consistency
 * - Bidirectional seek consistency
 * - Read all then verify
 */

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

// Include test globals - this configures which provider to use
#include "test_globals.h"

// Test configuration
namespace SeekTests {

constexpr bool TEST_FORWARD_SEEK_CONSISTENCY = true;
constexpr bool TEST_BACKWARD_SEEK_CONSISTENCY = true;
constexpr bool TEST_BIDIRECTIONAL_SEEK_CONSISTENCY = true;
constexpr bool TEST_READ_ALL_THEN_VERIFY = true;
constexpr bool TEST_MID_WORD_SEEK_RECONSTRUCT = true;
constexpr int MAX_WORDS = 500;
constexpr int MAX_FAILURES_TO_REPORT = 10;

// Store word info with position for verification
struct WordInfo {
  String word;
  int positionBefore;
  int positionAfter;
};

/**
 * Test: Forward word reading with seek verification
 */
void testForwardSeekConsistency(TestUtils::TestRunner& runner) {
  const char* testName = TestGlobals::getProviderName();
  std::cout << "\n=== Test: Forward Seek Consistency (" << testName << ") ===\n";

  TestGlobals::resetProvider();
  WordProvider& provider = TestGlobals::provider();

  std::vector<WordInfo> words;
  int wordCount = 0;

  std::cout << "  Reading words forward and recording positions...\n";

  while (provider.hasNextWord() && wordCount < MAX_WORDS) {
    WordInfo info;
    info.positionBefore = provider.getCurrentIndex();
    info.word = provider.getNextWord();
    info.positionAfter = provider.getCurrentIndex();

    if (info.word.length() == 0) {
      break;
    }

    words.push_back(info);
    wordCount++;

    if (wordCount % 100 == 0) {
      std::cout << "    Read " << wordCount << " words...\n";
    }
  }

  std::cout << "  Read " << words.size() << " words.\n";
  std::cout << "  Verifying seek to each position gives same word...\n";

  int failCount = 0;
  for (size_t i = 0; i < words.size() && failCount < MAX_FAILURES_TO_REPORT; i++) {
    const WordInfo& info = words[i];

    provider.setPosition(info.positionBefore);
    String wordAgain = provider.getNextWord();
    int positionAgain = provider.getCurrentIndex();

    if (wordAgain != info.word) {
      std::cout << "\n  *** WORD MISMATCH at index " << i << " ***\n";
      std::cout << "    Position: " << info.positionBefore << "\n";
      std::cout << "    Original word: \"" << info.word.c_str() << "\"\n";
      std::cout << "    After seek:    \"" << wordAgain.c_str() << "\"\n";
      failCount++;
      continue;
    }

    if (positionAgain != info.positionAfter) {
      std::cout << "\n  *** POSITION MISMATCH at index " << i << " ***\n";
      std::cout << "    Word: \"" << info.word.c_str() << "\"\n";
      std::cout << "    Original end pos: " << info.positionAfter << "\n";
      std::cout << "    After seek end pos: " << positionAgain << "\n";
      failCount++;
    }
  }

  if (failCount == 0) {
    std::cout << "  All " << words.size() << " words verified successfully!\n";
    runner.expectTrue(true, std::string(testName) + ": Forward seek consistency verified");
  } else {
    runner.expectTrue(false,
                      std::string(testName) + ": Forward seek failed for " + std::to_string(failCount) + " words");
  }
}

/**
 * Test: Backward word reading with seek verification
 */
void testBackwardSeekConsistency(TestUtils::TestRunner& runner) {
  const char* testName = TestGlobals::getProviderName();
  std::cout << "\n=== Test: Backward Seek Consistency (" << testName << ") ===\n";

  TestGlobals::resetProvider();
  WordProvider& provider = TestGlobals::provider();

  // First, read forward to get to the end
  std::cout << "  Reading words forward first...\n";
  int forwardCount = 0;
  while (provider.hasNextWord() && forwardCount < MAX_WORDS) {
    String word = provider.getNextWord();
    if (word.length() == 0)
      break;
    forwardCount++;
  }

  std::cout << "  Read " << forwardCount << " words forward.\n";

  // Now read backward from the end and record positions
  int endPosition = provider.getCurrentIndex();
  provider.setPosition(endPosition);

  std::vector<WordInfo> backwardWords;
  std::cout << "  Reading words backward from end position...\n";

  while (provider.getCurrentIndex() > 0 && (int)backwardWords.size() < MAX_WORDS) {
    WordInfo info;
    info.positionBefore = provider.getCurrentIndex();
    info.word = provider.getPrevWord();
    info.positionAfter = provider.getCurrentIndex();

    if (info.word.length() == 0) {
      break;
    }

    backwardWords.push_back(info);
  }

  std::cout << "  Read " << backwardWords.size() << " words backward.\n";

  // Verify seek consistency for backward reading
  std::cout << "  Verifying seek to each backward position gives same word...\n";

  int failCount = 0;
  for (size_t i = 0; i < backwardWords.size() && failCount < MAX_FAILURES_TO_REPORT; i++) {
    const WordInfo& info = backwardWords[i];

    provider.setPosition(info.positionBefore);
    String wordAgain = provider.getPrevWord();
    int positionAgain = provider.getCurrentIndex();

    if (wordAgain != info.word) {
      std::cout << "\n  *** BACKWARD WORD MISMATCH at index " << i << " ***\n";
      std::cout << "    Position before: " << info.positionBefore << "\n";
      std::cout << "    Original word: \"" << info.word.c_str() << "\"\n";
      std::cout << "    After seek:    \"" << wordAgain.c_str() << "\"\n";
      failCount++;
      continue;
    }

    if (positionAgain != info.positionAfter) {
      std::cout << "\n  *** BACKWARD POSITION MISMATCH at index " << i << " ***\n";
      std::cout << "    Word: \"" << info.word.c_str() << "\"\n";
      std::cout << "    Original end pos: " << info.positionAfter << "\n";
      std::cout << "    After seek end pos: " << positionAgain << "\n";
      failCount++;
    }
  }

  if (failCount == 0) {
    std::cout << "  All " << backwardWords.size() << " backward words verified successfully!\n";
    runner.expectTrue(true, std::string(testName) + ": Backward seek consistency verified");
  } else {
    runner.expectTrue(false,
                      std::string(testName) + ": Backward seek failed for " + std::to_string(failCount) + " words");
  }
}

/**
 * Test: Bidirectional consistency with round-trip verification
 */
void testBidirectionalSeekConsistency(TestUtils::TestRunner& runner) {
  const char* testName = TestGlobals::getProviderName();
  std::cout << "\n=== Test: Bidirectional Seek Consistency (" << testName << ") ===\n";

  TestGlobals::resetProvider();
  WordProvider& provider = TestGlobals::provider();

  // Read forward and collect words with positions
  std::vector<WordInfo> forwardWords;
  int maxWords = 300;

  std::cout << "  Phase 1: Reading " << maxWords << " words forward...\n";
  while (provider.hasNextWord() && (int)forwardWords.size() < maxWords) {
    WordInfo info;
    info.positionBefore = provider.getCurrentIndex();
    info.word = provider.getNextWord();
    info.positionAfter = provider.getCurrentIndex();
    if (info.word.length() == 0)
      break;
    forwardWords.push_back(info);
  }

  int endPosition = provider.getCurrentIndex();
  std::cout << "  Read " << forwardWords.size() << " words forward, ending at position " << endPosition << "\n";

  // Now read backward and collect words
  std::cout << "  Phase 2: Reading backward from end position...\n";
  std::vector<WordInfo> backwardWords;

  provider.setPosition(endPosition);

  while (provider.getCurrentIndex() > 0) {
    WordInfo info;
    info.positionBefore = provider.getCurrentIndex();
    info.word = provider.getPrevWord();
    info.positionAfter = provider.getCurrentIndex();
    if (info.word.length() == 0)
      break;
    backwardWords.push_back(info);
  }

  std::cout << "  Read " << backwardWords.size() << " words backward\n";

  // Filter whitespace-only tokens
  std::vector<WordInfo> filteredForward, filteredBackward;

  for (const auto& w : forwardWords) {
    if (w.word.length() > 0 && w.word[0] != ' ' && w.word[0] != '\t') {
      filteredForward.push_back(w);
    }
  }

  for (const auto& w : backwardWords) {
    if (w.word.length() > 0 && w.word[0] != ' ' && w.word[0] != '\t') {
      filteredBackward.push_back(w);
    }
  }

  std::cout << "  Comparing " << filteredForward.size() << " forward words with " << filteredBackward.size()
            << " backward words...\n";

  // Reverse backward list for comparison
  std::reverse(filteredBackward.begin(), filteredBackward.end());

  int compareCount = std::min(filteredForward.size(), filteredBackward.size());
  int failCount = 0;

  for (int i = 0; i < compareCount && failCount < MAX_FAILURES_TO_REPORT; i++) {
    if (filteredForward[i].word != filteredBackward[i].word) {
      std::cout << "\n  *** BIDIRECTIONAL MISMATCH at index " << i << " ***\n";
      std::cout << "    Forward word: \"" << filteredForward[i].word.c_str() << "\" @ "
                << filteredForward[i].positionBefore << "\n";
      std::cout << "    Backward word: \"" << filteredBackward[i].word.c_str() << "\" @ "
                << filteredBackward[i].positionBefore << "\n";
      failCount++;
    }
  }

  if (failCount == 0) {
    std::cout << "  All " << compareCount << " words match bidirectionally!\n";
    runner.expectTrue(true, std::string(testName) + ": Bidirectional consistency verified");
  } else {
    runner.expectTrue(false,
                      std::string(testName) + ": Bidirectional failed for " + std::to_string(failCount) + " words");
  }

  // Phase 3: Round-trip verification
  std::cout << "  Phase 3: Verifying position round-trip...\n";

  failCount = 0;
  int roundTripCount = std::min((int)filteredForward.size(), 100);

  for (int i = 0; i < roundTripCount && failCount < 5; i++) {
    const WordInfo& fw = filteredForward[i];

    provider.setPosition(fw.positionBefore);
    String wordForward = provider.getNextWord();
    int endPosForward = provider.getCurrentIndex();

    provider.setPosition(endPosForward);
    String wordBackward = provider.getPrevWord();

    if (wordForward != wordBackward) {
      std::cout << "\n  *** ROUND-TRIP MISMATCH at index " << i << " ***\n";
      std::cout << "    Start position: " << fw.positionBefore << "\n";
      std::cout << "    Forward read: \"" << wordForward.c_str() << "\"\n";
      std::cout << "    End position: " << endPosForward << "\n";
      std::cout << "    Backward read: \"" << wordBackward.c_str() << "\"\n";
      failCount++;
    }
  }

  if (failCount == 0) {
    std::cout << "  Round-trip verification passed!\n";
    runner.expectTrue(true, std::string(testName) + ": Round-trip consistency verified");
  } else {
    runner.expectTrue(false, std::string(testName) + ": Round-trip failed for " + std::to_string(failCount) + " words");
  }
}

/**
 * Test: Read all words then verify by seeking back
 */
void testReadAllThenVerify(TestUtils::TestRunner& runner) {
  const char* testName = TestGlobals::getProviderName();
  std::cout << "\n=== Test: Read All Then Verify (" << testName << ") ===\n";

  TestGlobals::resetProvider();
  WordProvider& provider = TestGlobals::provider();

  // Phase 1: Read ALL words and store their positions
  std::vector<WordInfo> allWords;

  std::cout << "  Phase 1: Reading all words and storing positions...\n";

  while (provider.hasNextWord()) {
    WordInfo info;
    info.positionBefore = provider.getCurrentIndex();
    info.word = provider.getNextWord();
    info.positionAfter = provider.getCurrentIndex();

    if (info.word.length() == 0) {
      break;
    }

    allWords.push_back(info);

    if (allWords.size() % 500 == 0) {
      std::cout << "    Read " << allWords.size() << " words...\n";
    }
  }

  // print out the last 10 words read for debugging
  std::cout << "    Last 10 words read:\n";
  size_t startIdx = (allWords.size() > 10) ? allWords.size() - 10 : 0;
  for (size_t i = startIdx; i < allWords.size(); i++) {
    const WordInfo& info = allWords[i];
    std::cout << "      Word " << i << ": \"" << info.word.c_str() << "\" @ " << info.positionBefore << "\n";
  }

  std::cout << "    Total words read: " << allWords.size() << "\n";
  std::cout << "    Final position: " << provider.getCurrentIndex() << "\n";

  // Phase 2: Verify each word by seeking to its position and reading again
  std::cout << "  Phase 2: Verifying each word by seeking and re-reading...\n";

  int failCount = 0;
  int maxFailsToReport = 20;

  for (size_t i = 0; i < allWords.size(); i++) {
    const WordInfo& info = allWords[i];

    provider.setPosition(info.positionBefore);
    String wordAgain = provider.getNextWord();
    int positionAgain = provider.getCurrentIndex();

    bool wordMatch = (wordAgain == info.word);
    // bool posMatch = (positionAgain == info.positionAfter);

    if (!wordMatch /* || !posMatch */) {
      failCount++;

      if (failCount <= maxFailsToReport) {
        std::cout << "\n  *** MISMATCH at word index " << i << " ***\n";
        std::cout << "    Seek to position: " << info.positionBefore << "\n";

        if (!wordMatch) {
          std::cout << "    Original word:    \"" << info.word.c_str() << "\" (len=" << info.word.length() << ")\n";
          std::cout << "    Re-read word:     \"" << wordAgain.c_str() << "\" (len=" << wordAgain.length() << ")\n";
        }
      }
    }

    if ((i + 1) % 1000 == 0) {
      std::cout << "    Verified " << (i + 1) << "/" << allWords.size() << " words, " << failCount
                << " failures so far...\n";
    }
  }

  std::cout << "\n  === Results ===\n";
  std::cout << "    Total words: " << allWords.size() << "\n";
  std::cout << "    Failures: " << failCount << "\n";

  if (failCount == 0) {
    std::cout << "    All words verified successfully!\n";
    runner.expectTrue(true, std::string(testName) + ": All words match after seek verification");
  } else {
    std::cout << "    FAILED: " << failCount << " words did not match after seeking\n";
    if (failCount > maxFailsToReport) {
      std::cout << "    (Only first " << maxFailsToReport << " failures shown)\n";
    }
    runner.expectTrue(false,
                      std::string(testName) + ": Verification failed for " + std::to_string(failCount) + " words");
  }
}

/**
 * Test: Seek into the middle of words and reconstruct by reading left and right
 *
 * This test:
 * 1. Reads words forward and records their positions
 * 2. For each word with length > 1, seeks to positions inside the word
 * 3. Reads left (getPrevWord) and right (getNextWord) from that position
 * 4. Verifies the concatenated result equals the original word
 */
void testMidWordSeekReconstruct(TestUtils::TestRunner& runner) {
  const char* testName = TestGlobals::getProviderName();
  std::cout << "\n=== Test: Mid-Word Seek Reconstruct (" << testName << ") ===\n";

  TestGlobals::resetProvider();
  WordProvider& provider = TestGlobals::provider();

  // First, read forward to collect words with their positions
  std::vector<WordInfo> words;
  std::cout << "  Phase 1: Reading words and recording positions...\n";

  while (provider.hasNextWord() && (int)words.size() < MAX_WORDS) {
    WordInfo info;
    info.positionBefore = provider.getCurrentIndex();
    info.word = provider.getNextWord();
    info.positionAfter = provider.getCurrentIndex();

    if (info.word.length() == 0) {
      break;
    }

    // Only collect actual words (not whitespace tokens)
    bool isWord = false;
    for (size_t i = 0; i < info.word.length(); i++) {
      char c = info.word[i];
      if (c != ' ' && c != '\n' && c != '\t' && c != '\r') {
        isWord = true;
        break;
      }
    }

    if (isWord && info.word.length() > 1) {
      words.push_back(info);
    }
  }

  std::cout << "  Found " << words.size() << " words (length > 1) to test.\n";

  // Phase 2: For each word, seek to middle positions and reconstruct
  std::cout << "  Phase 2: Testing mid-word seek and reconstruct...\n";

  int failCount = 0;
  int successCount = 0;
  int testedPositions = 0;

  for (size_t wordIdx = 0; wordIdx < words.size() && failCount < MAX_FAILURES_TO_REPORT; wordIdx++) {
    const WordInfo& info = words[wordIdx];
    int wordLen = info.positionAfter - info.positionBefore;

    // Test seeking to each position inside the word
    for (int offset = 1; offset < wordLen && failCount < MAX_FAILURES_TO_REPORT; offset++) {
      int midPosition = info.positionBefore + offset;
      testedPositions++;

      // Seek to mid-word position
      provider.setPosition(midPosition);

      // Verify we're inside a word
      if (!provider.isInsideWord()) {
        // This might be OK for some edge cases (e.g., position at word boundary)
        continue;
      }

      // Save position before reading
      int posBeforeLeft = provider.getCurrentIndex();

      // Read left part (from word start to current position)
      String leftPart = provider.getPrevWord();
      int posAfterLeft = provider.getCurrentIndex();

      // Now seek back to the mid position to read right
      provider.setPosition(midPosition);

      // Read right part (from current position to word end)
      String rightPart = provider.getNextWord();
      int posAfterRight = provider.getCurrentIndex();

      // Reconstruct the word
      String reconstructed = leftPart + rightPart;

      // Compare with original
      if (reconstructed != info.word) {
        std::cout << "\n  *** RECONSTRUCTION MISMATCH at word index " << wordIdx << " ***\n";
        std::cout << "    Original word:    \"" << info.word.c_str() << "\" (pos " << info.positionBefore << "-"
                  << info.positionAfter << ")\n";
        std::cout << "    Mid position:     " << midPosition << " (offset " << offset << ")\n";
        std::cout << "    Left part:        \"" << leftPart.c_str() << "\" (pos " << posBeforeLeft << "->"
                  << posAfterLeft << ")\n";
        std::cout << "    Right part:       \"" << rightPart.c_str() << "\" (pos " << midPosition << "->"
                  << posAfterRight << ")\n";
        std::cout << "    Reconstructed:    \"" << reconstructed.c_str() << "\"\n";
        failCount++;
      } else {
        successCount++;
      }
    }

    if ((wordIdx + 1) % 100 == 0) {
      std::cout << "    Processed " << (wordIdx + 1) << "/" << words.size() << " words...\n";
    }
  }

  std::cout << "\n  === Results ===\n";
  std::cout << "    Words tested: " << words.size() << "\n";
  std::cout << "    Positions tested: " << testedPositions << "\n";
  std::cout << "    Successful reconstructions: " << successCount << "\n";
  std::cout << "    Failures: " << failCount << "\n";

  if (failCount == 0) {
    std::cout << "    All mid-word seek reconstructions successful!\n";
    runner.expectTrue(true, std::string(testName) + ": Mid-word seek reconstruct verified");
  } else {
    std::cout << "    FAILED: " << failCount << " reconstructions did not match\n";
    runner.expectTrue(false, std::string(testName) + ": Mid-word seek reconstruct failed for " +
                                 std::to_string(failCount) + " positions");
  }
}

/**
 * Run all seek tests
 */
void runAllTests(TestUtils::TestRunner& runner) {
  if (TEST_FORWARD_SEEK_CONSISTENCY) {
    testForwardSeekConsistency(runner);
  }

  if (TEST_BACKWARD_SEEK_CONSISTENCY) {
    testBackwardSeekConsistency(runner);
  }

  if (TEST_BIDIRECTIONAL_SEEK_CONSISTENCY) {
    testBidirectionalSeekConsistency(runner);
  }

  if (TEST_READ_ALL_THEN_VERIFY) {
    testReadAllThenVerify(runner);
  }

  if (TEST_MID_WORD_SEEK_RECONSTRUCT) {
    testMidWordSeekReconstruct(runner);
  }
}

}  // namespace SeekTests

int main(int argc, char** argv) {
  TestUtils::TestRunner runner("Word Provider Seek Test Suite");

  // Initialize global provider and layout from test_globals.h configuration
  if (!TestGlobals::init()) {
    std::cerr << "Failed to initialize test globals\n";
    return 2;
  }

  // Run all seek tests using the configured provider
  SeekTests::runAllTests(runner);

  // Cleanup
  TestGlobals::cleanup();

  return runner.allPassed() ? 0 : 1;
}
