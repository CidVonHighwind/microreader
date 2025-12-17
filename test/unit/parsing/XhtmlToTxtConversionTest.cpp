/**
 * XhtmlToTxtConversionTest.cpp - XHTML to Plain Text Conversion Test
 *
 * Tests the EpubWordProvider's XHTML to plain text conversion logic.
 * Uses a test HTML file to verify correct handling of:
 * - Block elements (div, p, etc.)
 * - Empty block elements
 * - &nbsp; for intentional blank lines
 * - <br/> handling
 * - Whitespace normalization
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "content/providers/EpubWordProvider.h"
#include "content/xml/SimpleXmlParser.h"
#include "test_globals.h"
#include "test_utils.h"

namespace fs = std::filesystem;

// Path to test HTML file
const char* TEST_HTML_PATH = "C:/Users/Patrick/Desktop/microreader/resources/books/test.html";

/**
 * Read entire file contents as string
 */
std::string readFileContents(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return "";
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

/**
 * Print string with visible whitespace markers
 */
void printWithMarkers(const std::string& text) {
  std::cout << "--- Output (with markers) ---\n";
  for (size_t i = 0; i < text.length(); i++) {
    char c = text[i];
    if (c == '\n') {
      std::cout << "\\n\n";  // Show newline marker then actual newline
    } else if (c == ' ') {
      std::cout << "\xB7";  // Middle dot for space
    } else if (c == '\t') {
      std::cout << "\\t";
    } else {
      std::cout << c;
    }
  }
  std::cout << "\n--- End Output ---\n";
}

/**
 * Count occurrences of a substring
 */
int countOccurrences(const std::string& text, const std::string& substr) {
  int count = 0;
  size_t pos = 0;
  while ((pos = text.find(substr, pos)) != std::string::npos) {
    count++;
    pos += substr.length();
  }
  return count;
}

/**
 * Test: Convert test.html to plain text
 */
void testConversion(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: XHTML to TXT Conversion ===\n";

  // Check test file exists
  if (!fs::exists(TEST_HTML_PATH)) {
    std::cout << "ERROR: Test file not found: " << TEST_HTML_PATH << "\n";
    runner.expectTrue(false, "Test file should exist");
    return;
  }

  std::cout << "Input file: " << TEST_HTML_PATH << "\n";

  // Create EpubWordProvider with the test HTML (direct XHTML mode)
  EpubWordProvider provider(TEST_HTML_PATH);

  if (!provider.isValid()) {
    std::cout << "ERROR: Failed to create EpubWordProvider\n";
    runner.expectTrue(false, "Provider should be valid");
    return;
  }

  std::cout << "Provider created successfully\n";

  // The provider creates a .txt file next to the input
  std::string expectedTxtPath = TEST_HTML_PATH;
  size_t dotPos = expectedTxtPath.rfind('.');
  if (dotPos != std::string::npos) {
    expectedTxtPath = expectedTxtPath.substr(0, dotPos) + ".txt";
  }

  std::cout << "Expected output: " << expectedTxtPath << "\n";

  // Read the converted output
  std::string output = readFileContents(expectedTxtPath);

  if (output.empty()) {
    std::cout << "ERROR: Output file is empty or not found\n";
    runner.expectTrue(false, "Output should not be empty");
    return;
  }

  std::cout << "\n--- Raw Output ---\n";
  std::cout << output;
  std::cout << "\n--- End Raw Output ---\n\n";

  printWithMarkers(output);

  // ========== VALIDATION ==========
  std::cout << "\n=== Validation ===\n";

  // 1. Should contain "Das Buch"
  bool hasDasBuch = output.find("Das Buch") != std::string::npos;
  std::cout << "Contains 'Das Buch': " << (hasDasBuch ? "YES" : "NO") << "\n";
  runner.expectTrue(hasDasBuch, "Output should contain 'Das Buch'");

  // 2. Should contain "Los Angeles"
  bool hasLosAngeles = output.find("Los Angeles") != std::string::npos;
  std::cout << "Contains 'Los Angeles': " << (hasLosAngeles ? "YES" : "NO") << "\n";
  runner.expectTrue(hasLosAngeles, "Output should contain 'Los Angeles'");

  // 3. Should contain "Neal Stephenson"
  bool hasNealStephenson = output.find("Neal Stephenson") != std::string::npos;
  std::cout << "Contains 'Neal Stephenson': " << (hasNealStephenson ? "YES" : "NO") << "\n";
  runner.expectTrue(hasNealStephenson, "Output should contain 'Neal Stephenson'");

  // 4. Should NOT contain content from <head> or <style>
  bool hasStyleContent = output.find("margin-bottom") != std::string::npos;
  std::cout << "Contains style content: " << (hasStyleContent ? "YES (BAD)" : "NO (GOOD)") << "\n";
  runner.expectTrue(!hasStyleContent, "Output should NOT contain style content");

  // 5. Count newlines to verify structure
  int newlineCount = countOccurrences(output, "\n");
  std::cout << "Newline count: " << newlineCount << "\n";

  // 6. Check for blank line between "Das Buch" and "Los Angeles" (from &nbsp; div)
  // Pattern: "Das Buch\n\nLos Angeles" (two newlines = one blank line)
  // But with current test.html: Das Buch -> empty div with nbsp -> Los Angeles div
  // So we expect: "Das Buch\n" + blank line + "Los Angeles..."

  // Find positions
  size_t dasBuchPos = output.find("Das Buch");
  size_t losAngelesPos = output.find("Los Angeles");

  if (dasBuchPos != std::string::npos && losAngelesPos != std::string::npos) {
    std::string between = output.substr(dasBuchPos + 8, losAngelesPos - dasBuchPos - 8);
    int newlinesBetween = countOccurrences(between, "\n");
    std::cout << "Newlines between 'Das Buch' and 'Los Angeles': " << newlinesBetween << "\n";
    std::cout << "Text between (escaped): '";
    for (char c : between) {
      if (c == '\n')
        std::cout << "\\n";
      else if (c == ' ')
        std::cout << " ";
      else
        std::cout << c;
    }
    std::cout << "'\n";

    // Should have 2 newlines (blank line = div close + nbsp div close)
    runner.expectTrue(newlinesBetween == 2, "Should have blank line (2 newlines) between Das Buch and Los Angeles");
  }

  // 7. Check for blank line between Los Angeles paragraph and Neal Stephenson paragraph
  size_t nealPos = output.find("Neal Stephenson");
  size_t drohtPos = output.find("droht.");  // End of Los Angeles paragraph

  if (drohtPos != std::string::npos && nealPos != std::string::npos) {
    std::string between2 = output.substr(drohtPos + 6, nealPos - drohtPos - 6);
    int newlinesBetween2 = countOccurrences(between2, "\n");
    std::cout << "Newlines between paragraphs: " << newlinesBetween2 << "\n";
    std::cout << "Text between (escaped): '";
    for (char c : between2) {
      if (c == '\n')
        std::cout << "\\n";
      else if (c == ' ')
        std::cout << " ";
      else
        std::cout << c;
    }
    std::cout << "'\n";

    // Should have 2 newlines (blank line from &nbsp;<br/> div)
    runner.expectTrue(newlinesBetween2 == 2, "Should have blank line (2 newlines) between paragraphs");
  }

  // 8. Should NOT have trailing empty lines at the end (from empty mbppagebreak div)
  if (!output.empty()) {
    // Count trailing newlines
    int trailingNewlines = 0;
    for (int i = output.length() - 1; i >= 0 && output[i] == '\n'; i--) {
      trailingNewlines++;
    }
    std::cout << "Trailing newlines: " << trailingNewlines << "\n";
    runner.expectTrue(trailingNewlines <= 1, "Should have at most 1 trailing newline");
  }
}

/**
 * Test: Inline style stacking (bold + italic)
 */
void testInlineStyleStacking(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Inline Style Stacking ===\n";

  const char* stackHtmlPath = "C:/Users/Patrick/Desktop/microreader/resources/books/inline_stack_test.html";

  // Create a small test file with nested bold/italic
  std::string html =
      "<html><head><title>Stack</title></head><body>"
      "<p><b><i>Stacked</i></b></p>"
      "<p><i><b>Stacked2</b></i></p>"
      "<p><b>Outer <i>Inner</i> Outer</b></p>"
      "</body></html>";

  // Write file
  {
    std::ofstream out(stackHtmlPath);
    if (!out.is_open()) {
      std::cout << "ERROR: Failed to write test HTML: " << stackHtmlPath << "\n";
      runner.expectTrue(false, "Should be able to write test HTML");
      return;
    }
    out << html;
  }

  // Create provider
  EpubWordProvider provider(stackHtmlPath);
  if (!provider.isValid()) {
    std::cout << "ERROR: Failed to create EpubWordProvider for stacking test\n";
    runner.expectTrue(false, "Provider should be valid");
    return;
  }

  // Read converted TXT
  std::string expectedTxtPath = stackHtmlPath;
  size_t dotPos = expectedTxtPath.rfind('.');
  if (dotPos != std::string::npos) {
    expectedTxtPath = expectedTxtPath.substr(0, dotPos) + ".txt";
  }
  std::string output = readFileContents(expectedTxtPath);
  if (output.empty()) {
    std::cout << "ERROR: Output file is empty or not found for stacking test\n";
    runner.expectTrue(false, "Output should not be empty");
    return;
  }

  printWithMarkers(output);

  // Helper: check that near the word we have ESC+X before and ESC+x after
  auto checkWrapped = [&](const std::string& word) {
    size_t pos = output.find(word);
    if (pos == std::string::npos) {
      std::cout << "ERROR: Word not found: " << word << "\n";
      return false;
    }
    bool okBefore = false;
    if (pos >= 2 && output[pos - 2] == (char)0x1B && output[pos - 1] == 'X')
      okBefore = true;
    bool okAfter = false;
    size_t afterPos = pos + word.length();
    if (afterPos + 1 < output.length() && output[afterPos] == (char)0x1B && output[afterPos + 1] == 'x')
      okAfter = true;
    std::cout << "Check '" << word << "' : before(X)=" << (okBefore ? "YES" : "NO")
              << " after(x)=" << (okAfter ? "YES" : "NO") << "\n";
    runner.expectTrue(okBefore && okAfter, std::string("Word '") + word + " should be wrapped with X/x");
    return okBefore && okAfter;
  };

  checkWrapped("Stacked");
  checkWrapped("Stacked2");

  // For Inner, also expect a restore to B after the inner close (x then B)
  size_t innerPos = output.find("Inner");
  if (innerPos != std::string::npos) {
    bool beforeX = (innerPos >= 2 && output[innerPos - 2] == (char)0x1B && output[innerPos - 1] == 'X');
    bool afterXThenB = false;
    size_t afterInner = innerPos + 5;  // length of "Inner"
    if (afterInner + 3 < output.length() && output[afterInner] == (char)0x1B && output[afterInner + 1] == 'x' &&
        output[afterInner + 2] == (char)0x1B && output[afterInner + 3] == 'B') {
      afterXThenB = true;
    }
    std::cout << "Inner check: beforeX=" << (beforeX ? "YES" : "NO")
              << " after(x then B)=" << (afterXThenB ? "YES" : "NO") << "\n";
    runner.expectTrue(beforeX && afterXThenB, "Inner should be X then restored to B after closing inner element");
  } else {
    runner.expectTrue(false, "Inner not found");
  }
}

int main() {
  std::cout << "========================================\n";
  std::cout << "XHTML to TXT Conversion Test\n";
  std::cout << "========================================\n";

  TestUtils::TestRunner runner("XhtmlToTxtConversion");

  testConversion(runner);
  testInlineStyleStacking(runner);
  // New test: CSS base inline styles and inline overrides
  auto testInlineCssBaseAndOverrides = [&](TestUtils::TestRunner& r) {
    std::cout << "\n=== Test: Inline CSS base and overrides ===\n";
    const char* cssHtmlPath = "C:/Users/Patrick/Desktop/microreader/resources/books/inline_css_test.html";

    std::string html =
        "<html><head><title>CSS Base</title><style>\n"
        ".bold { font-weight: bold; }\n"
        ".italic { font-style: italic; }\n"
        ".bolditalic { font-weight: bold; font-style: italic; }\n"
        "</style></head><body>"
        "<p class=\"bold\">CssBold</p>"
        "<p class=\"bold\"><i>BaseAndInnerItalic</i></p>"
        "<p class=\"bold\">Outer <span style=\"font-weight: normal;\">InnerNormal</span> Outer</p>"
        "</body></html>";

    // Write file
    {
      std::ofstream out(cssHtmlPath);
      if (!out.is_open()) {
        std::cout << "ERROR: Failed to write test HTML: " << cssHtmlPath << "\n";
        r.expectTrue(false, "Should be able to write test HTML");
        return;
      }
      out << html;
    }

    EpubWordProvider provider(cssHtmlPath);
    r.expectTrue(provider.isValid(), "Provider should be valid for CSS base test");

    std::string expectedTxtPath = cssHtmlPath;
    size_t dotPos = expectedTxtPath.rfind('.');
    if (dotPos != std::string::npos) {
      expectedTxtPath = expectedTxtPath.substr(0, dotPos) + ".txt";
    }
    std::string output = readFileContents(expectedTxtPath);
    r.expectTrue(!output.empty(), "Output should not be empty for CSS base test");

    auto checkWrappedUpperLower = [&](const std::string& word, char open, char close) {
      size_t pos = output.find(word);
      if (pos == std::string::npos) {
        std::cout << "ERROR: Word not found: " << word << "\n";
        return false;
      }
      bool okBefore = false;
      if (pos >= 2 && output[pos - 2] == (char)0x1B && output[pos - 1] == open)
        okBefore = true;
      bool okAfter = false;
      size_t afterPos = pos + word.length();
      if (afterPos + 1 < output.length() && output[afterPos] == (char)0x1B && output[afterPos + 1] == close)
        okAfter = true;
      r.expectTrue(okBefore && okAfter, std::string("Word '") + word + " should be wrapped with expected tokens");
      return okBefore && okAfter;
    };

    // CssBold -> expect B/b
    checkWrappedUpperLower("CssBold", 'B', 'b');

    // BaseAndInnerItalic -> paragraph base bold + inner <i> should produce X/x
    checkWrappedUpperLower("BaseAndInnerItalic", 'X', 'x');

    // InnerNormal -> should be un-bolded inside span: before Inner should be ESC+b (reset) and after Inner should be
    // ESC+B (reopen)
    size_t posInner = output.find("InnerNormal");
    if (posInner != std::string::npos) {
      bool beforeReset = (posInner >= 2 && output[posInner - 2] == (char)0x1B && output[posInner - 1] == 'b');
      size_t afterInner = posInner + std::string("InnerNormal").length();
      bool afterReopen =
          (afterInner + 1 < output.length() && output[afterInner] == (char)0x1B && output[afterInner + 1] == 'B');
      r.expectTrue(beforeReset && afterReopen, "InnerNormal should be un-bolded then restore bold after span");
    } else {
      r.expectTrue(false, "InnerNormal not found");
    }
  };
  testInlineCssBaseAndOverrides(runner);

  // New test: text-indent -> spaces mapping
  auto testTextIndentMapping = [&](TestUtils::TestRunner& r) {
    std::cout << "\n=== Test: text-indent to spaces mapping ===\n";
    const char* indentHtmlPath = "C:/Users/Patrick/Desktop/microreader/resources/books/text_indent_test.html";

    std::string html =
        "<html><head><title>IndentTest</title></head><body>"
        "<p style=\"text-indent:20px\">PxIndent</p>"   // 20px -> round(20/8)=3
        "<p style=\"text-indent:1.5em\">EmIndent</p>"  // 1.5em -> 24px -> 3
        "<p style=\"text-indent:4px\">TinyIndent</p>"  // 4px -> round(4/8)=0 -> no indent
        "</body></html>";

    // Write file
    {
      std::ofstream out(indentHtmlPath);
      if (!out.is_open()) {
        std::cout << "ERROR: Failed to write test HTML: " << indentHtmlPath << "\n";
        r.expectTrue(false, "Should be able to write test HTML");
        return;
      }
      out << html;
    }

    EpubWordProvider provider(indentHtmlPath);
    r.expectTrue(provider.isValid(), "Provider should be valid for text-indent test");

    std::string expectedTxtPath = indentHtmlPath;
    size_t dotPos = expectedTxtPath.rfind('.');
    if (dotPos != std::string::npos) {
      expectedTxtPath = expectedTxtPath.substr(0, dotPos) + ".txt";
    }
    std::string output = readFileContents(expectedTxtPath);
    r.expectTrue(!output.empty(), "Output should not be empty for text-indent test");

    printWithMarkers(output);

    auto countLeadingSpacesBefore = [&](const std::string& needle) {
      size_t pos = output.find(needle);
      if (pos == std::string::npos)
        return -1;
      int spaces = 0;
      for (int i = (int)pos - 1; i >= 0; --i) {
        if (output[i] == '\n')
          break;
        if (output[i] == ' ')
          spaces++;
        else
          spaces = 0;
      }
      return spaces;
    };

    int sPx = countLeadingSpacesBefore("PxIndent");
    int sEm = countLeadingSpacesBefore("EmIndent");
    int sTiny = countLeadingSpacesBefore("TinyIndent");

    std::cout << "Spaces before PxIndent: " << sPx << "\n";
    std::cout << "Spaces before EmIndent: " << sEm << "\n";
    std::cout << "Spaces before TinyIndent: " << sTiny << "\n";

    r.expectTrue(sPx >= 3, "20px indent should map to >=3 spaces (round(px/8))");
    r.expectTrue(sEm >= 3, "1.5em indent should map to >=3 spaces");
    r.expectTrue(sTiny == 0, "4px indent should map to 0 spaces (ignored)");
  };
  testTextIndentMapping(runner);

  std::cout << "\n========================================\n";
  runner.printSummary();
  std::cout << "========================================\n";

  return runner.allPassed() ? 0 : 1;
}
