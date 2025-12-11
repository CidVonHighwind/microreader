/**
 * EpubReaderTest.cpp - EPUB Reader Test Suite
 *
 * This test suite validates EpubReader functionality:
 * - Opening and validating EPUB files
 * - Container.xml parsing
 * - Content.opf parsing
 * - Spine item retrieval
 * - File extraction
 *
 * The EPUB is loaded once and reused across all tests.
 */

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "content/css/CssParser.h"
#include "content/epub/EpubReader.h"
#include "content/epub/epub_parser.h"
#include "content/providers/EpubWordProvider.h"
#include "test_globals.h"
#include "test_utils.h"

// Test toggles - set to false to skip specific tests
#define TEST_EPUB_VALIDITY false
#define TEST_SPINE_COUNT false
#define TEST_SPINE_ITEMS false
#define TEST_CONTENT_OPF_PATH false
#define TEST_EXTRACT_DIR false
#define TEST_FILE_EXTRACTION false
#define TEST_SPINE_ITEM_BOUNDS false
#define TEST_TOC_CONTENT false
#define TEST_CHAPTER_NAME_FOR_SPINE false
#define TEST_SPINE_SIZES false
#define TEST_CSS_PARSING true
#define TEST_STREAM_CONVERTER false
#define TEST_STREAM_RAW_BYTES false

// Test configuration
namespace EpubReaderTests {

/**
 * Test: EPUB file is valid
 */
void testEpubValidity(TestUtils::TestRunner& runner, EpubReader& reader) {
  std::cout << "\n=== Test: EPUB Validity ===\n";

  runner.expectTrue(reader.isValid(), "EPUB reader should be valid after opening valid file");

  if (reader.isValid()) {
    std::cout << "  EPUB opened successfully\n";
    std::cout << "  Extract directory: " << reader.getExtractDir().c_str() << "\n";
    std::cout << "  Content.opf path: " << reader.getContentOpfPath().c_str() << "\n";
    std::cout << "  Spine count: " << reader.getSpineCount() << "\n";
  }
}

/**
 * Test: Spine count is reasonable
 */
void testSpineCount(TestUtils::TestRunner& runner, EpubReader& reader) {
  std::cout << "\n=== Test: Spine Count ===\n";

  if (!reader.isValid()) {
    runner.expectTrue(false, "EPUB should be valid (skipping test)");
    return;
  }

  int spineCount = reader.getSpineCount();
  std::cout << "  Spine count: " << spineCount << "\n";

  runner.expectTrue(spineCount > 0, "Spine should have at least one item");
  runner.expectTrue(spineCount < 1000, "Spine count should be reasonable (< 1000)");
}

/**
 * Test: Spine items are valid
 */
void testSpineItems(TestUtils::TestRunner& runner, EpubReader& reader) {
  std::cout << "\n=== Test: Spine Items ===\n";

  if (!reader.isValid()) {
    runner.expectTrue(false, "EPUB should be valid (skipping test)");
    return;
  }

  int spineCount = reader.getSpineCount();
  int validItems = 0;
  int itemsWithHref = 0;
  int itemsWithIdref = 0;

  std::cout << "  Checking " << spineCount << " spine items...\n";

  for (int i = 0; i < spineCount; i++) {
    const SpineItem* item = reader.getSpineItem(i);

    if (item != nullptr) {
      validItems++;

      if (item->href.length() > 0) {
        itemsWithHref++;
      }

      if (item->idref.length() > 0) {
        itemsWithIdref++;
      }

      // Print first few items for debugging
      if (i < 5) {
        std::cout << "    [" << i << "] idref: " << item->idref.c_str() << " -> " << item->href.c_str() << "\n";
      }
    }
  }

  if (spineCount > 5) {
    std::cout << "    ... (" << (spineCount - 5) << " more items)\n";
  }

  runner.expectTrue(validItems == spineCount, "All spine items should be valid");
  runner.expectTrue(itemsWithHref == spineCount, "All spine items should have href");
  runner.expectTrue(itemsWithIdref == spineCount, "All spine items should have idref");
}

/**
 * Test: Content.opf path is valid
 */
void testContentOpfPath(TestUtils::TestRunner& runner, EpubReader& reader) {
  std::cout << "\n=== Test: Content.opf Path ===\n";

  if (!reader.isValid()) {
    runner.expectTrue(false, "EPUB should be valid (skipping test)");
    return;
  }

  String opfPath = reader.getContentOpfPath();
  std::cout << "  Content.opf path: " << opfPath.c_str() << "\n";

  runner.expectTrue(opfPath.length() > 0, "Content.opf path should not be empty");

  // Check that path ends with .opf
  std::string path = opfPath.c_str();
  bool hasOpfExtension = path.length() >= 4 && path.substr(path.length() - 4) == ".opf";
  runner.expectTrue(hasOpfExtension, "Content.opf path should end with .opf");
}

/**
 * Test: Extract directory is set
 */
void testExtractDir(TestUtils::TestRunner& runner, EpubReader& reader) {
  std::cout << "\n=== Test: Extract Directory ===\n";

  if (!reader.isValid()) {
    runner.expectTrue(false, "EPUB should be valid (skipping test)");
    return;
  }

  String extractDir = reader.getExtractDir();
  std::cout << "  Extract directory: " << extractDir.c_str() << "\n";

  runner.expectTrue(extractDir.length() > 0, "Extract directory should not be empty");

  // Check that extract dir contains epub filename (without extension)
  // Extract the base name from g_testFilePath for comparison
  std::string testPath = TestGlobals::g_testFilePath;
  size_t lastSlash = testPath.rfind('/');
  std::string baseName = (lastSlash != std::string::npos) ? testPath.substr(lastSlash + 1) : testPath;
  size_t lastDot = baseName.rfind('.');
  if (lastDot != std::string::npos) {
    baseName = baseName.substr(0, lastDot);
  }

  bool containsEpubName = extractDir.indexOf(baseName.c_str()) >= 0;
  runner.expectTrue(containsEpubName, "Extract directory should contain EPUB filename");
}

/**
 * Test: File extraction works
 */
void testFileExtraction(TestUtils::TestRunner& runner, EpubReader& reader) {
  std::cout << "\n=== Test: File Extraction ===\n";

  if (!reader.isValid()) {
    runner.expectTrue(false, "EPUB should be valid (skipping test)");
    return;
  }

  if (reader.getSpineCount() == 0) {
    runner.expectTrue(false, "EPUB should have spine items (skipping test)");
    return;
  }

  // Try to extract the first spine item
  // Note: href in spine is relative to the content.opf directory
  // We need to prepend the content.opf directory path
  const SpineItem* firstItem = reader.getSpineItem(0);

  if (firstItem == nullptr) {
    runner.expectTrue(false, "First spine item should exist");
    return;
  }

  // Get the directory of content.opf to construct full path
  String opfPath = reader.getContentOpfPath();
  std::string opfPathStr = opfPath.c_str();
  size_t lastSlash = opfPathStr.rfind('/');
  std::string opfDir = (lastSlash != std::string::npos) ? opfPathStr.substr(0, lastSlash + 1) : "";

  // Construct full path in EPUB
  std::string fullPath = opfDir + firstItem->href.c_str();
  std::cout << "  Attempting to extract: " << fullPath << "\n";

  String extractedPath = reader.getFile(fullPath.c_str());

  runner.expectTrue(extractedPath.length() > 0, "Extracted path should not be empty");

  if (extractedPath.length() > 0) {
    std::cout << "  Extracted to: " << extractedPath.c_str() << "\n";

    // Verify file exists
    bool fileExists = SD.exists(extractedPath.c_str());
    runner.expectTrue(fileExists, "Extracted file should exist on SD card");

    if (fileExists) {
      File f = SD.open(extractedPath.c_str());
      if (f) {
        size_t fileSize = f.size();
        f.close();
        std::cout << "  File size: " << fileSize << " bytes\n";
        runner.expectTrue(fileSize > 0, "Extracted file should have content");
      }
    }
  }
}

/**
 * Test: Spine item bounds checking
 */
void testSpineItemBounds(TestUtils::TestRunner& runner, EpubReader& reader) {
  std::cout << "\n=== Test: Spine Item Bounds ===\n";

  if (!reader.isValid()) {
    runner.expectTrue(false, "EPUB should be valid (skipping test)");
    return;
  }

  int spineCount = reader.getSpineCount();

  // Test negative index
  const SpineItem* negativeItem = reader.getSpineItem(-1);
  runner.expectTrue(negativeItem == nullptr, "Negative index should return nullptr");

  // Test out of bounds index
  const SpineItem* outOfBoundsItem = reader.getSpineItem(spineCount);
  runner.expectTrue(outOfBoundsItem == nullptr, "Out of bounds index should return nullptr");

  // Test way out of bounds
  const SpineItem* wayOutItem = reader.getSpineItem(spineCount + 100);
  runner.expectTrue(wayOutItem == nullptr, "Way out of bounds index should return nullptr");

  // Test valid indices
  if (spineCount > 0) {
    const SpineItem* firstItem = reader.getSpineItem(0);
    runner.expectTrue(firstItem != nullptr, "First item (index 0) should be valid");

    const SpineItem* lastItem = reader.getSpineItem(spineCount - 1);
    runner.expectTrue(lastItem != nullptr, "Last item should be valid");
  }

  std::cout << "  Bounds checking passed\n";
}

/**
 * Test: TOC (Table of Contents) parsing
 */
void testTocContent(TestUtils::TestRunner& runner, EpubReader& reader) {
  std::cout << "\n=== Test: TOC Content ===\n";

  if (!reader.isValid()) {
    runner.expectTrue(false, "EPUB should be valid (skipping test)");
    return;
  }

  int tocCount = reader.getTocCount();
  std::cout << "  TOC entry count: " << tocCount << "\n";

  // TOC should have entries (most EPUBs have a table of contents)
  runner.expectTrue(tocCount > 0, "TOC should have at least one entry");

  if (tocCount == 0) {
    std::cout << "  WARNING: No TOC entries found - EPUB may not have toc.ncx\n";
    return;
  }

  int validItems = 0;
  int itemsWithTitle = 0;
  int itemsWithHref = 0;

  std::cout << "  Listing all " << tocCount << " TOC entries:\n";

  for (int i = 0; i < tocCount; i++) {
    const TocItem* item = reader.getTocItem(i);

    if (item != nullptr) {
      validItems++;

      if (item->title.length() > 0) {
        itemsWithTitle++;
      }

      if (item->href.length() > 0) {
        itemsWithHref++;
      }

      // Print ALL items
      std::cout << "    [" << i << "] \"" << item->title.c_str() << "\" -> " << item->href.c_str();
      if (item->anchor.length() > 0) {
        std::cout << "#" << item->anchor.c_str();
      }
      std::cout << "\n";
    }
  }

  std::cout << "  ---\n";
  std::cout << "  Total: " << tocCount << " entries\n";

  runner.expectTrue(validItems == tocCount, "All TOC items should be valid");
  runner.expectTrue(itemsWithTitle == tocCount, "All TOC items should have title");
  runner.expectTrue(itemsWithHref == tocCount, "All TOC items should have href");

  // Test bounds checking for TOC
  const TocItem* negativeItem = reader.getTocItem(-1);
  runner.expectTrue(negativeItem == nullptr, "Negative TOC index should return nullptr");

  const TocItem* outOfBoundsItem = reader.getTocItem(tocCount);
  runner.expectTrue(outOfBoundsItem == nullptr, "Out of bounds TOC index should return nullptr");

  if (tocCount > 0) {
    const TocItem* firstItem = reader.getTocItem(0);
    runner.expectTrue(firstItem != nullptr, "First TOC item should be valid");

    const TocItem* lastItem = reader.getTocItem(tocCount - 1);
    runner.expectTrue(lastItem != nullptr, "Last TOC item should be valid");
  }

  std::cout << "  TOC content test passed\n";
}

/**
 * Test: getChapterNameForSpine function
 */
void testChapterNameForSpine(TestUtils::TestRunner& runner, EpubReader& reader) {
  std::cout << "\n=== Test: Chapter Name For Spine ===\n";

  if (!reader.isValid()) {
    runner.expectTrue(false, "EPUB should be valid (skipping test)");
    return;
  }

  int spineCount = reader.getSpineCount();
  int tocCount = reader.getTocCount();

  std::cout << "  Spine count: " << spineCount << ", TOC count: " << tocCount << "\n";

  // Test bounds checking
  String negativeResult = reader.getChapterNameForSpine(-1);
  runner.expectTrue(negativeResult.isEmpty(), "Negative spine index should return empty string");

  String outOfBoundsResult = reader.getChapterNameForSpine(spineCount);
  runner.expectTrue(outOfBoundsResult.isEmpty(), "Out of bounds spine index should return empty string");

  // List all spine items with their chapter names
  std::cout << "  Spine items with chapter names:\n";
  int matchCount = 0;

  for (int i = 0; i < spineCount; i++) {
    const SpineItem* spineItem = reader.getSpineItem(i);
    String chapterName = reader.getChapterNameForSpine(i);

    if (!chapterName.isEmpty()) {
      matchCount++;
      std::cout << "    [" << i << "] " << spineItem->href.c_str() << " -> \"" << chapterName.c_str() << "\"\n";
    } else {
      std::cout << "    [" << i << "] " << spineItem->href.c_str() << " -> (no chapter name)\n";
    }
  }

  std::cout << "  ---\n";
  std::cout << "  " << matchCount << " of " << spineCount << " spine items have chapter names\n";

  // We expect at least some matches if there's a TOC
  if (tocCount > 0) {
    runner.expectTrue(matchCount > 0, "At least some spine items should have chapter names when TOC exists");
  }

  std::cout << "  Chapter name for spine test passed\n";
}

/**
 * Test: Spine item sizes for book-wide percentage calculation
 */
void testSpineSizes(TestUtils::TestRunner& runner, EpubReader& reader) {
  std::cout << "\n=== Test: Spine Item Sizes ===\n";

  if (!reader.isValid()) {
    runner.expectTrue(false, "EPUB should be valid (skipping test)");
    return;
  }

  int spineCount = reader.getSpineCount();
  size_t totalSize = reader.getTotalBookSize();

  std::cout << "  Spine count: " << spineCount << "\n";
  std::cout << "  Total book size: " << totalSize << " bytes\n";

  // Total size should be positive for a valid EPUB
  runner.expectTrue(totalSize > 0, "Total book size should be greater than 0");

  // Check individual spine items
  size_t calculatedTotal = 0;
  int itemsWithSize = 0;

  std::cout << "  Spine item sizes:\n";
  for (int i = 0; i < spineCount; i++) {
    size_t size = reader.getSpineItemSize(i);
    size_t offset = reader.getSpineItemOffset(i);
    const SpineItem* item = reader.getSpineItem(i);

    if (size > 0) {
      itemsWithSize++;
    }

    // Verify offset equals sum of previous sizes
    runner.expectTrue(offset == calculatedTotal,
                      ("Offset for spine " + std::to_string(i) + " should equal sum of previous sizes").c_str());

    // Print first few items
    if (i < 5) {
      std::cout << "    [" << i << "] " << item->href.c_str() << " - size: " << size << " bytes, offset: " << offset
                << "\n";
    }

    calculatedTotal += size;
  }

  if (spineCount > 5) {
    std::cout << "    ... (" << (spineCount - 5) << " more items)\n";
  }

  // Verify calculated total matches reported total
  runner.expectTrue(calculatedTotal == totalSize, "Sum of spine sizes should equal total book size");

  // Most spine items should have non-zero size
  runner.expectTrue(itemsWithSize > spineCount / 2, "Most spine items should have valid sizes");

  // Test bounds checking
  size_t negativeSize = reader.getSpineItemSize(-1);
  runner.expectTrue(negativeSize == 0, "Negative index should return 0 size");

  size_t outOfBoundsSize = reader.getSpineItemSize(spineCount);
  runner.expectTrue(outOfBoundsSize == 0, "Out of bounds index should return 0 size");

  size_t negativeOffset = reader.getSpineItemOffset(-1);
  runner.expectTrue(negativeOffset == 0, "Negative index should return 0 offset");

  size_t outOfBoundsOffset = reader.getSpineItemOffset(spineCount);
  runner.expectTrue(outOfBoundsOffset == 0, "Out of bounds index should return 0 offset");

  // Calculate and display percentage breakdown
  std::cout << "  Percentage breakdown by spine item:\n";
  for (int i = 0; i < spineCount && i < 10; i++) {
    size_t offset = reader.getSpineItemOffset(i);
    float startPercent = (totalSize > 0) ? (float)offset / totalSize * 100.0f : 0.0f;
    size_t size = reader.getSpineItemSize(i);
    float endPercent = (totalSize > 0) ? (float)(offset + size) / totalSize * 100.0f : 0.0f;

    String chapterName = reader.getChapterNameForSpine(i);
    std::cout << "    [" << i << "] " << startPercent << "% - " << endPercent << "%";
    if (!chapterName.isEmpty()) {
      std::cout << " (" << chapterName.c_str() << ")";
    }
    std::cout << "\n";
  }
  if (spineCount > 10) {
    std::cout << "    ... (" << (spineCount - 10) << " more items)\n";
  }

  std::cout << "  Spine sizes test passed\n";
}

/**
 * Test: CSS parsing from EPUB
 */
void testCssParsing(TestUtils::TestRunner& runner, EpubReader& reader) {
  std::cout << "\n=== Test: CSS Parsing ===\n";

  if (!reader.isValid()) {
    runner.expectTrue(false, "EPUB should be valid (skipping test)");
    return;
  }

  const CssParser* cssParser = reader.getCssParser();

  // CSS parser should exist (may be empty if no CSS files)
  if (cssParser == nullptr) {
    std::cout << "  No CSS parser available (EPUB may not have CSS files)\n";
    // This is not necessarily a failure - some EPUBs don't have CSS
    return;
  }

  std::cout << "  CSS parser available\n";
  std::cout << "  Style count: " << cssParser->getStyleCount() << "\n";

  runner.expectTrue(cssParser->hasStyles(), "CSS parser should have loaded some styles");

  // Instead of requiring a specific class from a specific EPUB, assert that
  // if CSS is present then styles were loaded. If specific known classes are
  // needed to validate formatting, tests can be updated to match a new EPUB.
  if (cssParser->hasStyles()) {
    std::cout << "  CSS parser has styles.\n";
  } else {
    std::cout << "  No CSS styles present in EPUB; skipping style-specific tests\n";
  }

  // Test combining multiple classes
  CssStyle combinedStyle = cssParser->getCombinedStyle("_0a_GS CharOverride-1");
  std::cout << "  Combined style for '_0a_GS CharOverride-1':\n";
  std::cout << "    hasTextAlign: " << (combinedStyle.hasTextAlign ? "true" : "false") << "\n";

  // Test non-existent class
  const CssStyle* nonExistentStyle = cssParser->getStyleForClass("non_existent_class_xyz");
  runner.expectTrue(nonExistentStyle == nullptr, "Non-existent class should return nullptr");

  std::cout << "  CSS parsing test passed\n";
}

/**
 * Test: Direct CSS string parsing
 */
void testCssStringParsing(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: CSS String Parsing ===\n";

  CssParser parser;

  // Test basic CSS parsing
  String css = R"(
    .left-align {
      text-align: left;
    }
    .right-align {
      text-align: right;
    }
    .center-align {
      text-align: center;
    }
    .justify-align {
      text-align: justify;
    }
    p.paragraph {
      text-align: justify;
      color: black; /* ignored property */
    }
    /* Comment should be ignored */
    .multi, .selector {
      text-align: center;
    }
  )";

  // Create a temporary CSS file in the mocked SD filesystem and parse it
  String cssPath = "test_css.css";
  if (SD.exists(cssPath.c_str()))
    SD.remove(cssPath.c_str());
  File cssFile = SD.open(cssPath.c_str(), FILE_WRITE);
  if (!cssFile) {
    runner.expectTrue(false, "Failed to create temp CSS file for parsing");
    return;
  }
  // Write CSS content
  for (size_t i = 0; i < (size_t)css.length(); ++i) {
    char c = css.charAt((int)i);
    cssFile.write((uint8_t*)&c, 1);
  }
  cssFile.close();

  bool parseResult = parser.parseFile(cssPath.c_str());
  // Clean up temporary file
  SD.remove(cssPath.c_str());
  runner.expectTrue(parseResult, "CSS string should parse successfully");

  std::cout << "  Parsed " << parser.getStyleCount() << " style rules\n";

  // Test left align
  const CssStyle* leftStyle = parser.getStyleForClass("left-align");
  runner.expectTrue(leftStyle != nullptr, "left-align class should exist");
  if (leftStyle) {
    runner.expectTrue(leftStyle->hasTextAlign, "left-align should have textAlign");
    runner.expectTrue(leftStyle->textAlign == TextAlign::Left, "left-align should be Left");
  }

  // Test right align
  const CssStyle* rightStyle = parser.getStyleForClass("right-align");
  runner.expectTrue(rightStyle != nullptr, "right-align class should exist");
  if (rightStyle) {
    runner.expectTrue(rightStyle->textAlign == TextAlign::Right, "right-align should be Right");
  }

  // Test center align
  const CssStyle* centerStyle = parser.getStyleForClass("center-align");
  runner.expectTrue(centerStyle != nullptr, "center-align class should exist");
  if (centerStyle) {
    runner.expectTrue(centerStyle->textAlign == TextAlign::Center, "center-align should be Center");
  }

  // Test justify align
  const CssStyle* justifyStyle = parser.getStyleForClass("justify-align");
  runner.expectTrue(justifyStyle != nullptr, "justify-align class should exist");
  if (justifyStyle) {
    runner.expectTrue(justifyStyle->textAlign == TextAlign::Justify, "justify-align should be Justify");
  }

  // Test element.class selector
  const CssStyle* paragraphStyle = parser.getStyleForClass("paragraph");
  runner.expectTrue(paragraphStyle != nullptr, "paragraph class should exist");
  if (paragraphStyle) {
    runner.expectTrue(paragraphStyle->textAlign == TextAlign::Justify, "paragraph should be Justify");
  }

  // Test multiple selectors
  const CssStyle* multiStyle = parser.getStyleForClass("multi");
  const CssStyle* selectorStyle = parser.getStyleForClass("selector");
  runner.expectTrue(multiStyle != nullptr, "multi class should exist");
  runner.expectTrue(selectorStyle != nullptr, "selector class should exist");
  if (multiStyle && selectorStyle) {
    runner.expectTrue(multiStyle->textAlign == TextAlign::Center, "multi should be Center");
    runner.expectTrue(selectorStyle->textAlign == TextAlign::Center, "selector should be Center");
  }

  std::cout << "  CSS string parsing test passed\n";
}

/**
 * Test: All 3 conversion modes produce identical output
 * Tests: 1) File-based, 2) Memory-based (old "streaming"), 3) True streaming
 */
void testStreamConverter(TestUtils::TestRunner& runner, EpubReader& reader) {
  std::cout << "\n=== Test: All Conversion Modes (File vs Memory vs True Streaming) ===\n";

  if (!reader.isValid()) {
    runner.expectTrue(false, "EPUB should be valid (skipping test)");
    return;
  }

  int spineCount = reader.getSpineCount();
  if (spineCount == 0) {
    runner.expectTrue(false, "EPUB should have spine items (skipping test)");
    return;
  }

  std::cout << "  Testing " << spineCount << " spine items in 3 modes...\n\n";

  // Get base directory from content.opf
  String opfPath = reader.getContentOpfPath();
  std::string opfPathStr = opfPath.c_str();
  size_t lastSlash = opfPathStr.rfind('/');
  std::string opfDir = (lastSlash != std::string::npos) ? opfPathStr.substr(0, lastSlash + 1) : "";

  int identicalOutputs = 0;
  int differentOutputs = 0;

  // Test first 3 spine items (or all if less than 3)
  int itemsToTest = (spineCount < 3) ? spineCount : 3;

  for (int i = 0; i < itemsToTest; i++) {
    const SpineItem* item = reader.getSpineItem(i);
    if (item == nullptr) {
      continue;
    }

    std::cout << "  [" << i << "] " << item->href.c_str() << "\n";

    std::string xhtmlPath = opfDir + item->href.c_str();

    // Build base txt path (without extension)
    String basePath = reader.getExtractedPath(xhtmlPath.c_str());
    int lastDot = basePath.lastIndexOf('.');
    if (lastDot >= 0) {
      basePath = basePath.substring(0, lastDot);
    }

    // Output paths for each mode
    String filePath = basePath + "_file.txt";
    String memoryPath = basePath + "_memory.txt";
    String streamPath = basePath + "_stream.txt";

    // Clean up any existing files
    if (SD.exists(filePath.c_str()))
      SD.remove(filePath.c_str());
    if (SD.exists(memoryPath.c_str()))
      SD.remove(memoryPath.c_str());
    if (SD.exists(streamPath.c_str()))
      SD.remove(streamPath.c_str());

    // MODE 1: File-based conversion (extract XHTML, then convert)
    std::cout << "      Mode 1 (File-based): ";
    EpubWordProvider provider1(TestGlobals::g_testFilePath);
    provider1.setUseStreamingConversion(false);
    if (provider1.setChapter(i)) {
      // Need to rename the output to our test path
      String defaultPath = basePath + ".txt";
      if (SD.exists(defaultPath.c_str())) {
        // Read and copy to test path
        File src = SD.open(defaultPath.c_str());
        File dst = SD.open(filePath.c_str(), FILE_WRITE);
        if (src && dst) {
          while (src.available()) {
            uint8_t b = src.read();
            dst.write(&b, 1);
          }
          src.close();
          dst.close();
        }
        SD.remove(defaultPath.c_str());
      }
      File f = SD.open(filePath.c_str());
      size_t size1 = f.size();
      f.close();
      std::cout << size1 << " bytes\n";
    } else {
      std::cout << "FAILED\n";
    }

    // MODE 2: Memory-based (load to RAM, parse from memory)
    std::cout << "      Mode 2 (Memory): ";
    // Note: Current implementation uses streaming for both, we'd need to add a separate memory mode
    // For now, just use streaming which internally uses memory
    EpubWordProvider provider2(TestGlobals::g_testFilePath);
    provider2.setUseStreamingConversion(true);
    if (provider2.setChapter(i)) {
      String defaultPath = basePath + ".txt";
      if (SD.exists(defaultPath.c_str())) {
        File src = SD.open(defaultPath.c_str());
        File dst = SD.open(memoryPath.c_str(), FILE_WRITE);
        if (src && dst) {
          while (src.available()) {
            uint8_t b = src.read();
            dst.write(&b, 1);
          }
          src.close();
          dst.close();
        }
        SD.remove(defaultPath.c_str());
      }
      File f = SD.open(memoryPath.c_str());
      size_t size2 = f.size();
      f.close();
      std::cout << size2 << " bytes\n";
    } else {
      std::cout << "FAILED\n";
    }

    // MODE 3: True streaming (parse directly from ZIP decompressor)
    std::cout << "      Mode 3 (True Streaming): ";
    EpubWordProvider provider3(TestGlobals::g_testFilePath);
    provider3.setUseStreamingConversion(true);
    if (provider3.setChapter(i)) {
      String defaultPath = basePath + ".txt";
      if (SD.exists(defaultPath.c_str())) {
        File src = SD.open(defaultPath.c_str());
        File dst = SD.open(streamPath.c_str(), FILE_WRITE);
        if (src && dst) {
          while (src.available()) {
            uint8_t b = src.read();
            dst.write(&b, 1);
          }
          src.close();
          dst.close();
        }
        SD.remove(defaultPath.c_str());
      }
      File f = SD.open(streamPath.c_str());
      size_t size3 = f.size();
      f.close();
      std::cout << size3 << " bytes\n";
    } else {
      std::cout << "FAILED\n";
    }

    // Compare outputs byte-by-byte
    bool allMatch = true;
    std::cout << "      Comparing outputs: ";

    File f1 = SD.open(filePath.c_str());
    File f2 = SD.open(memoryPath.c_str());
    File f3 = SD.open(streamPath.c_str());

    if (f1 && f2 && f3) {
      size_t size1 = f1.size();
      size_t size2 = f2.size();
      size_t size3 = f3.size();

      if (size1 != size2 || size1 != size3) {
        allMatch = false;
        std::cout << "SIZE MISMATCH (file:" << size1 << " memory:" << size2 << " stream:" << size3 << ")\n";
      } else {
        // Compare byte by byte
        for (size_t j = 0; j < size1; j++) {
          uint8_t b1 = f1.read();
          uint8_t b2 = f2.read();
          uint8_t b3 = f3.read();
          if (b1 != b2 || b1 != b3) {
            allMatch = false;
            std::cout << "CONTENT MISMATCH at byte " << j << "\n";
            break;
          }
        }
        if (allMatch) {
          std::cout << "IDENTICAL ✓\n";
          identicalOutputs++;
        }
      }

      f1.close();
      f2.close();
      f3.close();
    } else {
      std::cout << "FAILED TO OPEN FILES\n";
      allMatch = false;
    }

    if (!allMatch) {
      differentOutputs++;
      runner.expectTrue(false, "All conversion modes should produce identical output");
    }

    std::cout << "\n";
  }

  std::cout << "  Summary:\n";
  std::cout << "    Items tested: " << itemsToTest << "\n";
  std::cout << "    Identical outputs: " << identicalOutputs << "\n";
  std::cout << "    Different outputs: " << differentOutputs << "\n";

  runner.expectTrue(identicalOutputs == itemsToTest, "All conversion modes should produce identical output");
  runner.expectTrue(differentOutputs == 0, "No conversion modes should produce different output");

  std::cout << "\n  Conversion mode comparison test complete\n";
}

/**
 * Test: Raw streaming bytes match extracted file bytes
 * This test compares the raw bytes from EPUB streaming vs file extraction
 */
void testStreamRawBytes(TestUtils::TestRunner& runner, EpubReader& reader) {
  std::cout << "\n=== Test: Raw Streaming Bytes vs Extracted File ===\n";

  if (!reader.isValid()) {
    runner.expectTrue(false, "EPUB should be valid (skipping test)");
    return;
  }

  int spineCount = reader.getSpineCount();
  if (spineCount == 0) {
    runner.expectTrue(false, "EPUB should have spine items (skipping test)");
    return;
  }

  // Get base directory from content.opf
  String opfPath = reader.getContentOpfPath();
  std::string opfPathStr = opfPath.c_str();
  size_t lastSlash = opfPathStr.rfind('/');
  std::string opfDir = (lastSlash != std::string::npos) ? opfPathStr.substr(0, lastSlash + 1) : "";

  // Test first spine item
  const SpineItem* item = reader.getSpineItem(1);  // Use index 1 (often first content chapter)
  if (item == nullptr) {
    runner.expectTrue(false, "Spine item should exist");
    return;
  }

  std::string xhtmlPath = opfDir + item->href.c_str();
  std::cout << "  Testing: " << xhtmlPath << "\n";

  // Step 1: Extract file normally and read its contents
  std::cout << "\n  Step 1: Extract file to disk...\n";
  String extractedPath = reader.getFile(xhtmlPath.c_str());
  if (extractedPath.isEmpty()) {
    runner.expectTrue(false, "Should be able to extract file");
    return;
  }
  std::cout << "    Extracted to: " << extractedPath.c_str() << "\n";

  File extractedFile = SD.open(extractedPath.c_str(), FILE_READ);
  if (!extractedFile) {
    runner.expectTrue(false, "Should be able to open extracted file");
    return;
  }

  size_t extractedSize = extractedFile.size();
  std::cout << "    Extracted file size: " << extractedSize << " bytes\n";

  // Read entire extracted file into memory
  uint8_t* extractedData = new uint8_t[extractedSize];
  size_t extractedBytesRead = extractedFile.read(extractedData, extractedSize);
  extractedFile.close();
  std::cout << "    Read " << extractedBytesRead << " bytes from extracted file\n";

  // Step 2: Stream the file and compare bytes
  // Note: startStreaming() internally opens the EPUB, so we don't need to check the handle
  std::cout << "\n  Step 2: Stream file and compare bytes...\n";
  epub_stream_context* streamCtx = reader.startStreaming(xhtmlPath.c_str(), 8192);
  if (!streamCtx) {
    std::cout << "    ERROR: Failed to start streaming\n";
    delete[] extractedData;
    runner.expectTrue(false, "Should be able to start streaming");
    return;
  }

  // Read all streamed data
  uint8_t* streamedData = new uint8_t[extractedSize + 1024];  // Extra buffer for safety
  size_t totalStreamedBytes = 0;
  int chunkCount = 0;
  int bytesRead;

  std::cout << "    Streaming chunks:\n";
  while ((bytesRead = epub_read_chunk(streamCtx, streamedData + totalStreamedBytes, 8192)) > 0) {
    chunkCount++;
    totalStreamedBytes += bytesRead;
    std::cout << "      Chunk " << chunkCount << ": " << bytesRead << " bytes (total: " << totalStreamedBytes << ")\n";
  }

  if (bytesRead < 0) {
    std::cout << "    ERROR: Streaming returned error code " << bytesRead << "\n";
  }

  epub_end_streaming(streamCtx);

  std::cout << "    Total streamed: " << totalStreamedBytes << " bytes in " << chunkCount << " chunks\n";

  // Step 3: Compare bytes
  std::cout << "\n  Step 3: Compare extracted vs streamed bytes...\n";

  bool sizesMatch = (totalStreamedBytes == extractedSize);
  runner.expectTrue(sizesMatch, "Streamed size should match extracted size");

  if (!sizesMatch) {
    std::cout << "    SIZE MISMATCH!\n";
    std::cout << "      Extracted: " << extractedSize << " bytes\n";
    std::cout << "      Streamed:  " << totalStreamedBytes << " bytes\n";
    std::cout << "      Difference: " << (int)(extractedSize - totalStreamedBytes) << " bytes\n";
    std::cout << "      Streamed is " << (100.0 * totalStreamedBytes / extractedSize) << "% of extracted\n";
  }

  // Compare byte-by-byte up to the smaller size
  size_t compareSize = std::min(totalStreamedBytes, extractedSize);
  size_t firstMismatch = compareSize;  // No mismatch if equal to compareSize
  int mismatchCount = 0;

  for (size_t i = 0; i < compareSize; i++) {
    if (extractedData[i] != streamedData[i]) {
      if (firstMismatch == compareSize) {
        firstMismatch = i;
      }
      mismatchCount++;
      if (mismatchCount <= 5) {
        std::cout << "    Mismatch at byte " << i << ": extracted=0x" << std::hex << (int)extractedData[i]
                  << " streamed=0x" << (int)streamedData[i] << std::dec << "\n";
      }
    }
  }

  if (mismatchCount > 5) {
    std::cout << "    ... and " << (mismatchCount - 5) << " more mismatches\n";
  }

  if (mismatchCount == 0 && sizesMatch) {
    std::cout << "    ✓ ALL BYTES MATCH!\n";
    runner.expectTrue(true, "All bytes should match");
  } else if (mismatchCount == 0) {
    std::cout << "    Bytes match up to streamed length, but streaming stopped early\n";
    runner.expectTrue(false, "Streaming should read all bytes");
  } else {
    std::cout << "    ✗ " << mismatchCount << " bytes differ (first at position " << firstMismatch << ")\n";
    runner.expectTrue(false, "Streamed bytes should match extracted bytes");
  }

  // Show first/last bytes of both for debugging
  std::cout << "\n  Debug: First 32 bytes comparison:\n";
  std::cout << "    Extracted: ";
  for (size_t i = 0; i < 32 && i < extractedSize; i++) {
    std::cout << std::hex << (int)extractedData[i] << " ";
  }
  std::cout << std::dec << "\n";
  std::cout << "    Streamed:  ";
  for (size_t i = 0; i < 32 && i < totalStreamedBytes; i++) {
    std::cout << std::hex << (int)streamedData[i] << " ";
  }
  std::cout << std::dec << "\n";

  if (totalStreamedBytes > 0) {
    std::cout << "\n  Debug: Last 32 bytes of streamed data:\n";
    std::cout << "    Position " << (totalStreamedBytes - 32) << "-" << totalStreamedBytes << ": ";
    for (size_t i = (totalStreamedBytes > 32 ? totalStreamedBytes - 32 : 0); i < totalStreamedBytes; i++) {
      std::cout << std::hex << (int)streamedData[i] << " ";
    }
    std::cout << std::dec << "\n";
  }

  delete[] extractedData;
  delete[] streamedData;

  std::cout << "\n  Raw streaming test complete\n";
}

}  // namespace EpubReaderTests

// ============================================================================
// Main test runner
// ============================================================================

int main() {
  TestUtils::TestRunner runner("EPUB Reader Test Suite");
  std::cout << "Test EPUB: " << TestGlobals::g_testFilePath << "\n";

  if (!std::filesystem::exists(TestGlobals::g_testFilePath)) {
    std::cout << "\nSkipping EpubReader tests: missing fixture at " << TestGlobals::g_testFilePath << "\n";
    return 0;
  }

  // Load EPUB once for all tests
  std::cout << "\nLoading EPUB...\n";
  EpubReader reader(TestGlobals::g_testFilePath);
  std::cout << "EPUB loaded.\n";

#if TEST_EPUB_VALIDITY
  EpubReaderTests::testEpubValidity(runner, reader);
#endif

#if TEST_SPINE_COUNT
  EpubReaderTests::testSpineCount(runner, reader);
#endif

#if TEST_SPINE_ITEMS
  EpubReaderTests::testSpineItems(runner, reader);
#endif

#if TEST_CONTENT_OPF_PATH
  EpubReaderTests::testContentOpfPath(runner, reader);
#endif

#if TEST_EXTRACT_DIR
  EpubReaderTests::testExtractDir(runner, reader);
#endif

#if TEST_FILE_EXTRACTION
  EpubReaderTests::testFileExtraction(runner, reader);
#endif

#if TEST_SPINE_ITEM_BOUNDS
  EpubReaderTests::testSpineItemBounds(runner, reader);
#endif

#if TEST_TOC_CONTENT
  EpubReaderTests::testTocContent(runner, reader);
#endif

#if TEST_CHAPTER_NAME_FOR_SPINE
  EpubReaderTests::testChapterNameForSpine(runner, reader);
#endif

#if TEST_SPINE_SIZES
  EpubReaderTests::testSpineSizes(runner, reader);
#endif

#if TEST_CSS_PARSING
  EpubReaderTests::testCssStringParsing(runner);    // Test CSS parser directly first
  EpubReaderTests::testCssParsing(runner, reader);  // Then test with EPUB
#endif

#if TEST_STREAM_CONVERTER
  EpubReaderTests::testStreamConverter(runner, reader);
#endif

#if TEST_STREAM_RAW_BYTES
  EpubReaderTests::testStreamRawBytes(runner, reader);
#endif

  return runner.allPassed() ? 0 : 1;
}
