/**
 * FileWordProviderNavigationTest.cpp - Dedicated FileWordProvider Navigation Tests
 *
 * This test suite validates forward/backward navigation in FileWordProvider
 * using the existing navigation_test.txt file. Tests cover:
 *
 * Test cases:
 * 1. Basic forward/backward navigation consistency
 * 2. ESC token handling (alignment and style tokens)
 * 3. Position consistency after navigation
 * 4. Round-trip navigation (forward -> backward -> forward)
 * 5. Small buffer stress test
 * 6. Unicode content handling
 * 7. Specific content verification
 */

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "WString.h"
#include "content/providers/FileWordProvider.h"
#include "test_config.h"
#include "test_utils.h"

namespace FileWordProviderNavigationTests {

// Test file path - uses existing navigation_test.txt file
const char* TEST_FILE_PATH = "test/data/navigation_test.txt";

// Structure to hold word info for comparison
struct WordInfo {
  std::string text;
  int positionBefore;
  int positionAfter;
  FontStyle style;
  TextAlign alignment;

  bool operator==(const WordInfo& other) const {
    return text == other.text;
  }
};

// Helper to convert FontStyle to string
const char* fontStyleToString(FontStyle style) {
  switch (style) {
    case FontStyle::REGULAR:
      return "regular";
    case FontStyle::BOLD:
      return "bold";
    case FontStyle::ITALIC:
      return "italic";
    case FontStyle::BOLD_ITALIC:
      return "bold_italic";
    default:
      return "unknown";
  }
}

// Helper to convert TextAlign to string
const char* textAlignToString(TextAlign align) {
  switch (align) {
    case TextAlign::Left:
      return "left";
    case TextAlign::Right:
      return "right";
    case TextAlign::Center:
      return "center";
    case TextAlign::Justify:
      return "justify";
    default:
      return "unknown";
  }
}

/**
 * Helper to escape string for display
 */
std::string escapeForDisplay(const std::string& s) {
  std::string result;
  for (char c : s) {
    switch (c) {
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      case ' ':
        result += "<SP>";
        break;
      case 27:  // ESC character
        result += "<ESC>";
        break;
      default:
        result += c;
    }
  }
  return result;
}

/**
 * Collect all words forward from a provider
 */
std::vector<WordInfo> collectWordsForward(FileWordProvider& provider) {
  std::vector<WordInfo> words;
  provider.setPosition(0);

  while (provider.hasNextWord()) {
    WordInfo info;
    info.positionBefore = provider.getCurrentIndex();
    info.alignment = provider.getParagraphAlignment();
    StyledWord sw = provider.getNextWord();
    info.positionAfter = provider.getCurrentIndex();
    info.text = sw.text.c_str();
    info.style = sw.style;

    if (sw.text.length() == 0)
      break;
    words.push_back(info);
  }
  return words;
}

/**
 * Collect all words backward from end of file
 */
std::vector<WordInfo> collectWordsBackward(FileWordProvider& provider) {
  std::vector<WordInfo> words;

  // First go to end
  provider.setPosition(0);
  while (provider.hasNextWord()) {
    String word = provider.getNextWord().text;
    if (word.length() == 0)
      break;
  }
  int endPos = provider.getCurrentIndex();

  // Now collect backward
  provider.setPosition(endPos);
  while (provider.hasPrevWord()) {
    WordInfo info;
    info.positionAfter = provider.getCurrentIndex();
    StyledWord sw = provider.getPrevWord();
    info.positionBefore = provider.getCurrentIndex();
    // Get alignment AFTER getPrevWord() so index_ is at the word's position
    info.alignment = provider.getParagraphAlignment();
    info.text = sw.text.c_str();
    info.style = sw.style;

    if (sw.text.length() == 0)
      break;
    words.push_back(info);
  }

  // Reverse to get correct order
  std::reverse(words.begin(), words.end());
  return words;
}

/**
 * Compare forward and backward word lists (text only)
 */
bool compareWordLists(const std::vector<WordInfo>& forward, const std::vector<WordInfo>& backward,
                      std::string& errorMsg) {
  if (forward.size() != backward.size()) {
    std::ostringstream ss;
    ss << "Word count mismatch: forward=" << forward.size() << ", backward=" << backward.size();
    errorMsg = ss.str();
    return false;
  }

  for (size_t i = 0; i < forward.size(); i++) {
    if (forward[i].text != backward[i].text) {
      std::ostringstream ss;
      ss << "Word mismatch at index " << i << ": forward='" << escapeForDisplay(forward[i].text) << "' backward='"
         << escapeForDisplay(backward[i].text) << "'";
      errorMsg = ss.str();
      return false;
    }
  }
  return true;
}

/**
 * Helper to check if a word is whitespace-only
 */
bool isWhitespaceOnly(const std::string& word) {
  for (char c : word) {
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
      return false;
  }
  return true;
}

/**
 * Compare forward and backward word lists including style and alignment
 */
bool compareWordListsWithStyle(const std::vector<WordInfo>& forward, const std::vector<WordInfo>& backward,
                               int& wordMismatches, int& styleMismatches, int& alignmentMismatches,
                               int maxReports = 10) {
  wordMismatches = 0;
  styleMismatches = 0;
  alignmentMismatches = 0;

  size_t compareCount = std::min(forward.size(), backward.size());

  for (size_t i = 0; i < compareCount; i++) {
    const WordInfo& fw = forward[i];
    const WordInfo& bw = backward[i];

    if (fw.text != bw.text) {
      if (wordMismatches < maxReports) {
        std::cout << "  *** Word mismatch at index " << i << ": forward='" << escapeForDisplay(fw.text)
                  << "' backward='" << escapeForDisplay(bw.text) << "'\n";
      }
      wordMismatches++;
    }

    // Check style match (skip whitespace-only words)
    if (fw.style != bw.style && !isWhitespaceOnly(fw.text)) {
      if (styleMismatches < maxReports) {
        std::cout << "  *** Style mismatch at index " << i << " (word='" << escapeForDisplay(fw.text)
                  << "'): forward=" << fontStyleToString(fw.style) << " backward=" << fontStyleToString(bw.style)
                  << "\n";
      }
      styleMismatches++;
    }

    // Check alignment match (skip whitespace-only words)
    if (fw.alignment != bw.alignment && !isWhitespaceOnly(fw.text)) {
      if (alignmentMismatches < maxReports) {
        std::cout << "  *** Alignment mismatch at index " << i << " (word='" << escapeForDisplay(fw.text)
                  << "'): forward=" << textAlignToString(fw.alignment)
                  << " backward=" << textAlignToString(bw.alignment) << "\n";
      }
      alignmentMismatches++;
    }
  }

  return (wordMismatches == 0 && styleMismatches == 0 && alignmentMismatches == 0);
}

// ============================================================================
// Test Case 1: Basic forward/backward navigation consistency
// ============================================================================
void testBasicNavigation(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Basic Navigation Consistency ===\n";

  FileWordProvider provider(TEST_FILE_PATH, 1024);

  auto forward = collectWordsForward(provider);
  auto backward = collectWordsBackward(provider);

  std::string errorMsg;
  bool match = compareWordLists(forward, backward, errorMsg);

  if (!match) {
    std::cout << "  Forward words: " << forward.size() << "\n";
    std::cout << "  First few words:\n";
    for (size_t i = 0; i < std::min(size_t(10), forward.size()); i++) {
      std::cout << "    [" << i << "] '" << escapeForDisplay(forward[i].text) << "'\n";
    }
  }
  runner.expectTrue(match, "Basic navigation: forward/backward match", errorMsg);

  // Should have many words since the file contains substantial content
  bool hasSubstantialWords = forward.size() > 100;
  if (!hasSubstantialWords) {
    std::cout << "  Got " << forward.size() << " words (expected > 100)\n";
  }
  runner.expectTrue(hasSubstantialWords, "Basic navigation: substantial word count",
                    "Got " + std::to_string(forward.size()) + " words");
}

// ============================================================================
// Test Case 2: ESC token handling
// ============================================================================
void testEscTokenHandling(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: ESC Token Processing ===\n";

  FileWordProvider provider(TEST_FILE_PATH, 1024);

  // Test that ESC tokens (ESC + command byte) are processed for formatting, not returned as words
  provider.setPosition(0);

  // Collect first words
  std::vector<std::string> firstWords;
  for (int i = 0; i < 20 && provider.hasNextWord(); i++) {
    String word = provider.getNextWord().text;
    if (word.length() == 0)
      break;
    firstWords.push_back(word.c_str());
  }

  // Check that ESC character (0x1B) is NOT in any word (tokens should be consumed for formatting)
  bool foundEscInWord = false;
  for (const auto& word : firstWords) {
    if (word.find('\x1B') != std::string::npos) {
      foundEscInWord = true;
      std::cout << "  Found ESC in word: '" << escapeForDisplay(word) << "'\n";
      break;
    }
  }

  runner.expectTrue(!foundEscInWord, "ESC token processing: tokens consumed for formatting, not returned as words");

  // Check that we can find normal content words (proving the file is readable)
  bool foundContentWord = false;
  for (const auto& word : firstWords) {
    if (word == "Navigation" || word == "Test" || word == "Document") {
      foundContentWord = true;
      break;
    }
  }

  runner.expectTrue(foundContentWord, "ESC token processing: normal content words found");
}

// ============================================================================
// Test Case 3: Position consistency after navigation
// ============================================================================
void testPositionConsistency(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Position Consistency ===\n";

  FileWordProvider provider(TEST_FILE_PATH, 1024);

  // Collect forward with positions, but only test a subset for performance
  auto forward = collectWordsForward(provider);

  // Test first 20 words for position consistency
  size_t testWords = std::min(size_t(20), forward.size());

  bool allPositionsValid = true;
  std::string errorMsg;

  // Test: seek to each position and read forward should give same word
  for (size_t i = 0; i < testWords; i++) {
    provider.setPosition(forward[i].positionBefore);
    String word = provider.getNextWord().text;
    if (std::string(word.c_str()) != forward[i].text) {
      std::ostringstream ss;
      ss << "Position " << forward[i].positionBefore << ": expected '" << escapeForDisplay(forward[i].text) << "' got '"
         << escapeForDisplay(word.c_str()) << "'";
      errorMsg = ss.str();
      allPositionsValid = false;
      break;
    }
  }

  if (!allPositionsValid) {
    std::cout << "  Tested " << testWords << " words for position consistency\n";
  }
  runner.expectTrue(allPositionsValid, "Position consistency: seek and read forward", errorMsg);
}

// ============================================================================
// Test Case 4: Round-trip navigation
// ============================================================================
void testRoundTrip(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Round-Trip Navigation ===\n";

  FileWordProvider provider(TEST_FILE_PATH, 1024);

  // Read forward, then back, then forward again
  provider.setPosition(0);

  // Collect first 50 words going forward for performance
  std::vector<std::string> firstPass;
  int wordCount = 0;
  while (provider.hasNextWord() && wordCount < 50) {
    String word = provider.getNextWord().text;
    if (word.length() == 0)
      break;
    firstPass.push_back(word.c_str());
    wordCount++;
  }

  int endPos = provider.getCurrentIndex();

  // Now go backward from that position
  provider.setPosition(endPos);
  std::vector<std::string> backwardPass;
  wordCount = 0;
  while (provider.hasPrevWord() && wordCount < 50) {
    String word = provider.getPrevWord().text;
    if (word.length() == 0)
      break;
    backwardPass.push_back(word.c_str());
    wordCount++;
  }
  std::reverse(backwardPass.begin(), backwardPass.end());

  // Go forward again
  provider.setPosition(0);
  std::vector<std::string> secondPass;
  wordCount = 0;
  while (provider.hasNextWord() && wordCount < 50) {
    String word = provider.getNextWord().text;
    if (word.length() == 0)
      break;
    secondPass.push_back(word.c_str());
    wordCount++;
  }

  // All three should match
  bool firstBackwardMatch = (firstPass == backwardPass);
  bool firstSecondMatch = (firstPass == secondPass);

  if (!firstBackwardMatch || !firstSecondMatch) {
    std::cout << "  Tested round-trip with " << firstPass.size() << " words\n";
  }
  runner.expectTrue(firstBackwardMatch, "Round-trip: forward matches backward");
  runner.expectTrue(firstSecondMatch, "Round-trip: first forward matches second forward");
}

// ============================================================================
// Test Case 5: Small buffer stress test
// ============================================================================
void testSmallBuffer(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Small Buffer Stress Test ===\n";

  // Use a very small buffer to force buffer sliding with the real file
  FileWordProvider provider(TEST_FILE_PATH, 64);

  auto forward = collectWordsForward(provider);
  auto backward = collectWordsBackward(provider);

  std::string errorMsg;
  bool match = compareWordLists(forward, backward, errorMsg);

  if (!match) {
    std::cout << "  Forward words: " << forward.size() << "\n";
  }
  runner.expectTrue(match, "Small buffer: forward/backward match with small buffer", errorMsg);
}

// ============================================================================
// Test Case 6: Unicode content handling
// ============================================================================
void testUnicodeContent(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Unicode Content ===\n";

  FileWordProvider provider(TEST_FILE_PATH, 1024);

  auto forward = collectWordsForward(provider);

  // Look for unicode characters in the content (the file contains German umlauts and French accents)
  bool foundUnicode = false;
  for (const auto& word : forward) {
    // Check for some unicode characters that should be in the test file
    if (word.text.find("ä") != std::string::npos || word.text.find("ö") != std::string::npos ||
        word.text.find("ü") != std::string::npos || word.text.find("é") != std::string::npos ||
        word.text.find("è") != std::string::npos) {
      foundUnicode = true;
      break;
    }
  }

  if (!foundUnicode) {
    std::cout << "  No unicode characters found in content\n";
  }
  runner.expectTrue(foundUnicode, "Unicode content: found expected unicode characters");

  // Test navigation consistency with unicode content
  auto backward = collectWordsBackward(provider);
  std::string errorMsg;
  bool match = compareWordLists(forward, backward, errorMsg);
  runner.expectTrue(match, "Unicode content: forward/backward match", errorMsg);
}

// ============================================================================
// Test Case 7: Specific content verification
// ============================================================================
void testSpecificContent(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Specific Content Verification ===\n";

  FileWordProvider provider(TEST_FILE_PATH, 1024);

  auto forward = collectWordsForward(provider);

  // Check for specific words that should be in the navigation_test.txt file
  std::vector<std::string> expectedWords = {"Navigation", "Test", "Document", "provider", "forward", "backward"};

  int foundCount = 0;
  for (const std::string& expected : expectedWords) {
    bool found = false;
    for (const auto& word : forward) {
      if (word.text == expected) {
        found = true;
        foundCount++;
        break;
      }
    }
    if (!found && foundCount < 4) {
      std::cout << "  Missing expected word: '" << expected << "'\n";
    }
  }

  bool foundMost = foundCount >= 4;
  if (!foundMost) {
    std::cout << "  Found " << foundCount << " of " << expectedWords.size() << " expected words\n";
  }
  runner.expectTrue(foundMost, "Specific content: found most expected words",
                    "Found " + std::to_string(foundCount) + " of " + std::to_string(expectedWords.size()));
}

// ============================================================================
// Test Case 8: Style and Alignment consistency
// ============================================================================
void testStyleConsistency(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Style and Alignment Consistency ===\n";

  FileWordProvider provider(TEST_FILE_PATH, 1024);

  auto forward = collectWordsForward(provider);
  auto backward = collectWordsBackward(provider);

  // Print style summary for forward pass
  std::cout << "  Forward pass style summary:\n";
  int normalCount = 0, boldCount = 0, italicCount = 0, boldItalicCount = 0;
  for (const auto& word : forward) {
    if (isWhitespaceOnly(word.text))
      continue;
    switch (word.style) {
      case FontStyle::REGULAR:
        normalCount++;
        break;
      case FontStyle::BOLD:
        boldCount++;
        break;
      case FontStyle::ITALIC:
        italicCount++;
        break;
      case FontStyle::BOLD_ITALIC:
        boldItalicCount++;
        break;
    }
  }
  std::cout << "    Normal: " << normalCount << ", Bold: " << boldCount << ", Italic: " << italicCount
            << ", BoldItalic: " << boldItalicCount << "\n";

  // Compare with style checking
  int wordMismatches, styleMismatches, alignmentMismatches;
  bool allMatch =
      compareWordListsWithStyle(forward, backward, wordMismatches, styleMismatches, alignmentMismatches, 15);

  std::cout << "  Comparison results:\n";
  std::cout << "    Word mismatches: " << wordMismatches << "\n";
  std::cout << "    Style mismatches: " << styleMismatches << "\n";
  std::cout << "    Alignment mismatches: " << alignmentMismatches << "\n";

  runner.expectTrue(wordMismatches == 0, "Style consistency: word text matches");
  runner.expectTrue(styleMismatches == 0, "Style consistency: styles match between forward/backward",
                    "Got " + std::to_string(styleMismatches) + " style mismatches");
  runner.expectTrue(alignmentMismatches == 0, "Style consistency: alignments match between forward/backward",
                    "Got " + std::to_string(alignmentMismatches) + " alignment mismatches");

  // Print first few styled words for verification
  std::cout << "  Sample styled words (forward):\n";
  int printCount = 0;
  for (size_t i = 0; i < forward.size() && printCount < 10; i++) {
    const auto& w = forward[i];
    if (!isWhitespaceOnly(w.text) && (w.style != FontStyle::REGULAR || w.alignment != TextAlign::Left)) {
      std::cout << "    [" << i << "] '" << escapeForDisplay(w.text) << "' style=" << fontStyleToString(w.style)
                << " align=" << textAlignToString(w.alignment) << "\n";
      printCount++;
    }
  }
}

// ============================================================================
// Test Case 9: Style parsing with generated test file
// ============================================================================
void testStyleParsingWithGeneratedFile(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Style Parsing with Generated File ===\n";

  // Create a test file with known ESC tokens
  const char* testFilePath = "test/output/style_test_generated.txt";

  // ESC token constants
  const char ESC = '\x1B';

  // Build test content with ESC tokens
  // Format: ESC + command byte
  // Alignment: L=left, R=right, C=center, J=justify
  // Style: B=bold on, b=bold off, I=italic on, i=italic off, X=bold+italic on, x=bold+italic off

  std::ofstream outFile(testFilePath, std::ios::binary);
  if (!outFile) {
    runner.expectTrue(false, "Style parsing: could not create test file");
    return;
  }

  // Paragraph 1: Left aligned with multiple bold words
  outFile << ESC << 'L';  // Left align
  outFile << "Normal start here ";
  outFile << ESC << 'B';  // Bold on
  outFile << "bold one bold two bold three bold four ";
  outFile << ESC << 'b';  // Bold off
  outFile << "back to normal text\n";

  // Paragraph 2: Center aligned with style immediately after alignment
  outFile << ESC << 'C' << ESC << 'I';  // Center align + Italic on (adjacent)
  outFile << "Italic centered start ";
  outFile << "italic alpha italic beta italic gamma italic delta ";
  outFile << ESC << 'i';  // Italic off
  outFile << "centered end\n";

  // Paragraph 3: Right aligned with bold+italic immediately after alignment
  outFile << ESC << 'R' << ESC << 'X';  // Right align + Bold+italic on (adjacent)
  outFile << "BoldItalic right start";
  outFile << "combo first combo second combo third combo fourth combo fifth ";
  outFile << ESC << 'x';  // Bold+italic off
  outFile << "right suffix\n";

  // Paragraph 4: Justify with interleaved styles
  outFile << ESC << 'J';  // Justify
  outFile << "Start plain ";
  outFile << ESC << 'B';  // Bold on
  outFile << "bold A bold B bold C ";
  outFile << ESC << 'b';  // Bold off
  outFile << "middle plain ";
  outFile << ESC << 'I';  // Italic on
  outFile << "italic X italic Y italic Z ";
  outFile << ESC << 'i';  // Italic off
  outFile << "end plain\n";

  // Paragraph 5: Nested style transitions (bold then bold+italic then italic)
  outFile << ESC << 'C';  // Center align
  outFile << "Transition test ";
  outFile << ESC << 'B';  // Bold on
  outFile << "only bold here ";
  outFile << ESC << 'X';  // Bold+italic on (replaces bold)
  outFile << "now both styles ";
  outFile << ESC << 'I';  // Italic on (replaces bold+italic with italic)
  outFile << "just italic now ";
  outFile << ESC << 'i';  // Italic off
  outFile << "final normal";
  outFile << ESC << 'b' << "\n";  // Style off adjacent to newline

  // Paragraph 6: Style change immediately after space
  outFile << ESC << 'R' << ESC << 'B';  // Right align + Bold on (adjacent)
  outFile << "Bold from start ";
  outFile << ESC << 'b';  // Bold off
  outFile << "normal middle ";
  outFile << ESC << 'I';  // Italic on
  outFile << "italic after ";
  outFile << ESC << 'i';  // Italic off
  outFile << "normal again\n";

  // Paragraph 7: Style change immediately before newline
  outFile << ESC << 'J';  // Justify
  outFile << "Before newline ";
  outFile << ESC << 'I';  // Italic on
  outFile << "italic ends here";
  outFile << ESC << 'i';  // Italic off right before newline
  outFile << "\n";

  // Paragraph 8: Style at start of line (right after newline)
  outFile << ESC << 'C' << ESC << 'B';  // Center align + Bold on (adjacent)
  outFile << "Bold at line start ";
  outFile << ESC << 'b';  // Bold off
  outFile << "then normal\n";

  // Paragraph 9: Multiple spaces with style changes
  outFile << ESC << 'R';  // Right align
  outFile << "Word1 ";
  outFile << ESC << 'B';  // Bold on
  outFile << " ";         // Space while bold
  outFile << "BoldWord ";
  outFile << ESC << 'b';  // Bold off
  outFile << " Word2\n";  // Space then word

  // Paragraph 10: Style token between words (no space separation)
  outFile << ESC << 'J';  // Justify
  outFile << "NoSpace";
  outFile << ESC << 'B';  // Bold on - no space before
  outFile << "Bold";      // No space after token
  outFile << ESC << 'b';  // Bold off
  outFile << "Normal\n";

  // Paragraph 11: Multiple newlines with adjacent align+style
  outFile << ESC << 'C';  // Center align
  outFile << "Before empty\n";
  outFile << "\n";                      // Empty line
  outFile << ESC << 'L' << ESC << 'B';  // Left align + Bold on (adjacent after empty line)
  outFile << "After empty bold\n";
  outFile << ESC << 'b';  // Bold off

  // Paragraph 12: Tab characters with adjacent align+style
  outFile << ESC << 'J' << ESC << 'I';  // Justify + Italic on (adjacent)
  outFile << "ItalicTab\t";
  outFile << ESC << 'i';  // Italic off
  outFile << "normalTab\t";
  outFile << ESC << 'B';  // Bold on
  outFile << "boldTab\t";
  outFile << ESC << 'b';  // Bold off
  outFile << "normal\n";

  outFile.close();

  // Now test with the generated file
  FileWordProvider provider(testFilePath, 1024);

  auto forward = collectWordsForward(provider);
  auto backward = collectWordsBackward(provider);

  // Print what we got (abbreviated)
  std::cout << "  Generated file word count: " << forward.size() << "\n";
  std::cout << "  First 10 words (forward):\n";
  for (size_t i = 0; i < std::min(size_t(10), forward.size()); i++) {
    const auto& w = forward[i];
    std::cout << "    [" << i << "] '" << escapeForDisplay(w.text) << "' style=" << fontStyleToString(w.style)
              << " align=" << textAlignToString(w.alignment) << "\n";
  }

  // Count styles (skip whitespace)
  int regularCount = 0, boldCount = 0, italicCount = 0, boldItalicCount = 0;
  for (const auto& w : forward) {
    if (isWhitespaceOnly(w.text))
      continue;
    switch (w.style) {
      case FontStyle::REGULAR:
        regularCount++;
        break;
      case FontStyle::BOLD:
        boldCount++;
        break;
      case FontStyle::ITALIC:
        italicCount++;
        break;
      case FontStyle::BOLD_ITALIC:
        boldItalicCount++;
        break;
    }
  }

  std::cout << "  Style counts: Regular=" << regularCount << " Bold=" << boldCount << " Italic=" << italicCount
            << " BoldItalic=" << boldItalicCount << "\n";

  // Verify we found multiple styled words
  runner.expectTrue(boldCount >= 4, "Style parsing: found multiple bold words",
                    "Expected >= 4 bold words, got " + std::to_string(boldCount));
  runner.expectTrue(italicCount >= 4, "Style parsing: found multiple italic words",
                    "Expected >= 4 italic words, got " + std::to_string(italicCount));
  runner.expectTrue(boldItalicCount >= 4, "Style parsing: found multiple bold+italic words",
                    "Expected >= 4 bold+italic words, got " + std::to_string(boldItalicCount));

  // Compare forward/backward
  int wordMismatches, styleMismatches, alignmentMismatches;
  compareWordListsWithStyle(forward, backward, wordMismatches, styleMismatches, alignmentMismatches, 20);

  std::cout << "  Forward vs Backward comparison:\n";
  std::cout << "    Word mismatches: " << wordMismatches << "\n";
  std::cout << "    Style mismatches: " << styleMismatches << "\n";
  std::cout << "    Alignment mismatches: " << alignmentMismatches << "\n";

  if (styleMismatches > 0) {
    std::cout << "  Backward words for comparison:\n";
    for (size_t i = 0; i < std::min(size_t(20), backward.size()); i++) {
      const auto& w = backward[i];
      std::cout << "    [" << i << "] '" << escapeForDisplay(w.text) << "' style=" << fontStyleToString(w.style)
                << " align=" << textAlignToString(w.alignment) << "\n";
    }
  }

  runner.expectTrue(wordMismatches == 0, "Style parsing: word text matches forward/backward");
  runner.expectTrue(styleMismatches == 0, "Style parsing: styles match forward/backward",
                    "Got " + std::to_string(styleMismatches) + " style mismatches");
  runner.expectTrue(alignmentMismatches == 0, "Style parsing: alignments match forward/backward",
                    "Got " + std::to_string(alignmentMismatches) + " alignment mismatches");

  // ========== Test jumping into middle of styled blocks ==========
  std::cout << "\n  --- Testing seek into styled blocks ---\n";

  // Find a word in the middle of a bold block
  int boldMiddleIndex = -1;
  for (size_t i = 0; i < forward.size(); i++) {
    if (forward[i].text == "two" && forward[i].style == FontStyle::BOLD) {
      boldMiddleIndex = static_cast<int>(i);
      break;
    }
  }

  if (boldMiddleIndex >= 0) {
    // Seek to position of "bold two" and read forward
    provider.setPosition(forward[boldMiddleIndex].positionBefore);
    StyledWord sw = provider.getNextWord();
    std::cout << "  Seek to 'two': got '" << sw.text.c_str() << "' style=" << fontStyleToString(sw.style) << "\n";
    runner.expectTrue(std::string(sw.text.c_str()) == "two", "Seek into bold: correct word");
    runner.expectTrue(sw.style == FontStyle::BOLD, "Seek into bold: correct style",
                      "Expected bold, got " + std::string(fontStyleToString(sw.style)));

    // Now go backward from middle of bold block
    provider.setPosition(forward[boldMiddleIndex].positionAfter);
    StyledWord swBack = provider.getPrevWord();
    std::cout << "  Backward from 'two': got '" << swBack.text.c_str() << "' style=" << fontStyleToString(swBack.style)
              << "\n";
    runner.expectTrue(std::string(swBack.text.c_str()) == "two", "Backward from bold middle: correct word");
    runner.expectTrue(swBack.style == FontStyle::BOLD, "Backward from bold middle: correct style",
                      "Expected bold, got " + std::string(fontStyleToString(swBack.style)));
  } else {
    std::cout << "  Could not find 'two' with bold style for seek test\n";
  }

  // Find a word in the middle of an italic block
  int italicMiddleIndex = -1;
  for (size_t i = 0; i < forward.size(); i++) {
    if (forward[i].text == "beta" && forward[i].style == FontStyle::ITALIC) {
      italicMiddleIndex = static_cast<int>(i);
      break;
    }
  }

  if (italicMiddleIndex >= 0) {
    // Seek to position of "italic beta" and read forward
    provider.setPosition(forward[italicMiddleIndex].positionBefore);
    StyledWord sw = provider.getNextWord();
    std::cout << "  Seek to 'beta': got '" << sw.text.c_str() << "' style=" << fontStyleToString(sw.style) << "\n";
    runner.expectTrue(std::string(sw.text.c_str()) == "beta", "Seek into italic: correct word");
    runner.expectTrue(sw.style == FontStyle::ITALIC, "Seek into italic: correct style",
                      "Expected italic, got " + std::string(fontStyleToString(sw.style)));

    // Now go backward from middle of italic block
    provider.setPosition(forward[italicMiddleIndex].positionAfter);
    StyledWord swBack = provider.getPrevWord();
    std::cout << "  Backward from 'beta': got '" << swBack.text.c_str() << "' style=" << fontStyleToString(swBack.style)
              << "\n";
    runner.expectTrue(std::string(swBack.text.c_str()) == "beta", "Backward from italic middle: correct word");
    runner.expectTrue(swBack.style == FontStyle::ITALIC, "Backward from italic middle: correct style",
                      "Expected italic, got " + std::string(fontStyleToString(swBack.style)));
  } else {
    std::cout << "  Could not find 'beta' with italic style for seek test\n";
  }

  // Find a word in the middle of a bold+italic block
  int comboMiddleIndex = -1;
  for (size_t i = 0; i < forward.size(); i++) {
    if (forward[i].text == "third" && forward[i].style == FontStyle::BOLD_ITALIC) {
      comboMiddleIndex = static_cast<int>(i);
      break;
    }
  }

  if (comboMiddleIndex >= 0) {
    // Seek to position and read forward
    provider.setPosition(forward[comboMiddleIndex].positionBefore);
    StyledWord sw = provider.getNextWord();
    std::cout << "  Seek to 'third': got '" << sw.text.c_str() << "' style=" << fontStyleToString(sw.style) << "\n";
    runner.expectTrue(std::string(sw.text.c_str()) == "third", "Seek into bold+italic: correct word");
    runner.expectTrue(sw.style == FontStyle::BOLD_ITALIC, "Seek into bold+italic: correct style",
                      "Expected bold_italic, got " + std::string(fontStyleToString(sw.style)));

    // Now go backward
    provider.setPosition(forward[comboMiddleIndex].positionAfter);
    StyledWord swBack = provider.getPrevWord();
    std::cout << "  Backward from 'third': got '" << swBack.text.c_str()
              << "' style=" << fontStyleToString(swBack.style) << "\n";
    runner.expectTrue(std::string(swBack.text.c_str()) == "third", "Backward from bold+italic middle: correct word");
    runner.expectTrue(swBack.style == FontStyle::BOLD_ITALIC, "Backward from bold+italic middle: correct style",
                      "Expected bold_italic, got " + std::string(fontStyleToString(swBack.style)));
  } else {
    std::cout << "  Could not find 'third' with bold+italic style for seek test\n";
  }
}

// ============================================================================
// Run all tests
// ============================================================================
void runAllTests(TestUtils::TestRunner& runner) {
  testBasicNavigation(runner);
  testEscTokenHandling(runner);
  testPositionConsistency(runner);
  testRoundTrip(runner);
  testSmallBuffer(runner);
  testUnicodeContent(runner);
  testSpecificContent(runner);
  testStyleConsistency(runner);
  testStyleParsingWithGeneratedFile(runner);
}

}  // namespace FileWordProviderNavigationTests

int main(int argc, char** argv) {
  TestUtils::TestRunner runner("FileWordProvider Navigation Test Suite");

  std::cout << "========================================\n";
  std::cout << "FileWordProvider Navigation Tests\n";
  std::cout << "========================================\n";

  FileWordProviderNavigationTests::runAllTests(runner);

  std::cout << "\nNote: Tests use existing navigation_test.txt file\n";

  return runner.allPassed() ? 0 : 1;
}
