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

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

#include "content/epub/EpubReader.h"
#include "test_config.h"
#include "test_utils.h"

// Test toggles - set to false to skip specific tests
#define TEST_EPUB_VALIDITY true
#define TEST_SPINE_COUNT true
#define TEST_SPINE_ITEMS true
#define TEST_CONTENT_OPF_PATH true
#define TEST_EXTRACT_DIR true
#define TEST_FILE_EXTRACTION true
#define TEST_SPINE_ITEM_BOUNDS true
#define TEST_TOC_CONTENT true
#define TEST_CHAPTER_NAME_FOR_SPINE true
#define TEST_SPINE_SIZES true

// Test configuration
namespace EpubReaderTests {

// Path to test EPUB file
const char* TEST_EPUB_PATH = "data/books/Bobiverse 1.epub";

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

  // Check that extract dir contains epub filename
  bool containsEpubName = extractDir.indexOf("Bobiverse") >= 0;
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

}  // namespace EpubReaderTests

// ============================================================================
// Main test runner
// ============================================================================

int main() {
  TestUtils::TestRunner runner("EPUB Reader Test Suite");
  std::cout << "Test EPUB: " << EpubReaderTests::TEST_EPUB_PATH << "\n";

  if (!std::filesystem::exists(EpubReaderTests::TEST_EPUB_PATH)) {
    std::cout << "\nSkipping EpubReader tests: missing fixture at " << EpubReaderTests::TEST_EPUB_PATH
              << "\n";
    return 0;
  }

  // Load EPUB once for all tests
  std::cout << "\nLoading EPUB...\n";
  EpubReader reader(EpubReaderTests::TEST_EPUB_PATH);
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

  return runner.allPassed() ? 0 : 1;
}
