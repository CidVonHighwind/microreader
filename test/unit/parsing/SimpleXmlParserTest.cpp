#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "content/epub/EpubReader.h"
#include "content/epub/epub_parser.h"
#include "content/xml/SimpleXmlParser.h"
#include "test_globals.h"
#include "test_utils.h"

struct NodeSnapshot {
  SimpleXmlParser::NodeType type;
  String name;
  bool isEmpty = false;
  String text;
  size_t filePosStart;
  size_t filePosEnd;
};

String readTextForward(SimpleXmlParser& parser) {
  String text = "";
  while (parser.hasMoreTextChars()) {
    text += parser.readTextNodeCharForward();
  }
  return text;
}

std::vector<NodeSnapshot> readForwardNodes(TestUtils::TestRunner& runner, const char* path) {
  SimpleXmlParser parser;
  std::vector<NodeSnapshot> result;

  runner.expectTrue(parser.open(path), "Open XHTML for forward pass", "", true);
  while (parser.read()) {
    NodeSnapshot snap;
    snap.type = parser.getNodeType();
    snap.name = parser.getName();
    snap.isEmpty = parser.isEmptyElement();
    snap.filePosStart = parser.getElementStartPos();
    snap.filePosEnd = parser.getElementEndPos();

    if (snap.type == SimpleXmlParser::Text) {
      snap.text = readTextForward(parser);
    }

    result.push_back(snap);
  }
  parser.close();
  return result;
}

std::vector<NodeSnapshot> readForwardNodesFromMemory(TestUtils::TestRunner& runner, const char* data, size_t dataSize) {
  SimpleXmlParser parser;
  std::vector<NodeSnapshot> result;

  std::cout << "  Testing openFromMemory with " << dataSize << " bytes\n";

  bool opened = parser.openFromMemory(data, dataSize);
  std::cout << "  openFromMemory returned: " << (opened ? "true" : "false") << "\n";

  if (!opened) {
    std::cout << "  ERROR: Failed to open from memory!\n";
    return result;
  }

  std::cout << "  Starting to read nodes...\n";
  int nodeCount = 0;
  while (parser.read()) {
    nodeCount++;
    NodeSnapshot snap;
    snap.type = parser.getNodeType();
    snap.name = parser.getName();
    snap.isEmpty = parser.isEmptyElement();
    snap.filePosStart = parser.getElementStartPos();
    snap.filePosEnd = parser.getElementEndPos();

    if (snap.type == SimpleXmlParser::Text) {
      snap.text = readTextForward(parser);
    }

    result.push_back(snap);

    // Show first few nodes for debugging
    if (nodeCount <= 10) {
      std::cout << "    Node " << nodeCount << ": ";
      if (snap.type == SimpleXmlParser::Element) {
        std::cout << "Element <" << snap.name.c_str() << ">";
      } else if (snap.type == SimpleXmlParser::EndElement) {
        std::cout << "EndElement </" << snap.name.c_str() << ">";
      } else if (snap.type == SimpleXmlParser::Text) {
        std::cout << "Text: \"" << snap.text.substring(0, 30).c_str() << (snap.text.length() > 30 ? "..." : "") << "\"";
      }
      std::cout << "\n";
    }
  }

  std::cout << "  Total nodes read: " << nodeCount << "\n";
  parser.close();
  return result;
}

// Wrapper for EPUB stream callback with byte tracking
struct StreamCallbackContext {
  epub_stream_context* epubStream;
  size_t totalBytesRead;
  int callCount;
};

// EPUB stream callback for testing
int epubStreamCallback(char* buffer, size_t maxSize, void* userData) {
  StreamCallbackContext* ctx = (StreamCallbackContext*)userData;
  int bytesRead = epub_read_chunk(ctx->epubStream, (uint8_t*)buffer, maxSize);
  ctx->callCount++;
  std::cout << "    [Stream callback #" << ctx->callCount << "] maxSize=" << maxSize << " bytesRead=" << bytesRead
            << " total=" << (ctx->totalBytesRead + (bytesRead > 0 ? bytesRead : 0)) << "\n";
  if (bytesRead > 0) {
    ctx->totalBytesRead += bytesRead;
  }
  return bytesRead;
}

std::vector<NodeSnapshot> readForwardNodesFromStream(TestUtils::TestRunner& runner, const char* epubPath,
                                                     int spineIndex) {
  SimpleXmlParser parser;
  std::vector<NodeSnapshot> result;

  std::cout << "  Testing openFromStream from EPUB spine item " << spineIndex << "\n";

  // Open EPUB reader (takes path in constructor, not via open())
  EpubReader reader(epubPath);
  if (!reader.isValid()) {
    std::cout << "  ERROR: Failed to open EPUB\n";
    return result;
  }

  // Get spine item (SpineItem is a global struct, not EpubReader::SpineItem)
  const SpineItem* spineItem = reader.getSpineItem(spineIndex);
  if (!spineItem) {
    std::cout << "  ERROR: Invalid spine index\n";
    return result;
  }

  // Build full path: content.opf is at OEBPS/content.opf, so hrefs are relative to OEBPS/
  String contentOpfPath = reader.getContentOpfPath();
  String baseDir = "";
  int lastSlash = contentOpfPath.lastIndexOf('/');
  if (lastSlash >= 0) {
    baseDir = contentOpfPath.substring(0, lastSlash + 1);
  }
  String fullHref = baseDir + spineItem->href;

  std::cout << "  Streaming from: " << fullHref.c_str() << "\n";

  // Get file info before streaming to check uncompressed size
  uint32_t fileIndex;
  epub_error err = epub_locate_file(reader.getReader(), fullHref.c_str(), &fileIndex);
  if (err == EPUB_OK) {
    epub_file_info info;
    epub_get_file_info(reader.getReader(), fileIndex, &info);
    std::cout << "  EPUB file index: " << fileIndex << "\n";
    std::cout << "  Compressed size: " << info.compressed_size << "\n";
    std::cout << "  Uncompressed size: " << info.uncompressed_size << "\n";
    std::cout << "  Compression: " << info.compression << "\n";
  } else {
    std::cout << "  ERROR: Could not locate file in EPUB\n";
  }

  // Start streaming from spine item using full path
  epub_stream_context* epubStreamCtx = reader.startStreaming(fullHref.c_str());
  if (!epubStreamCtx) {
    std::cout << "  ERROR: Failed to start EPUB streaming\n";
    return result;
  }

  // Create callback context for byte tracking
  StreamCallbackContext callbackCtx;
  callbackCtx.epubStream = epubStreamCtx;
  callbackCtx.totalBytesRead = 0;
  callbackCtx.callCount = 0;

  bool opened = parser.openFromStream(epubStreamCallback, &callbackCtx);
  std::cout << "  openFromStream returned: " << (opened ? "true" : "false") << "\n";

  if (!opened) {
    std::cout << "  ERROR: Failed to open from stream!\n";
    return result;
  }

  std::cout << "  Starting to read nodes...\n";
  int nodeCount = 0;
  while (parser.read()) {
    nodeCount++;
    NodeSnapshot snap;
    snap.type = parser.getNodeType();
    snap.name = parser.getName();
    snap.isEmpty = parser.isEmptyElement();
    snap.filePosStart = parser.getElementStartPos();
    snap.filePosEnd = parser.getElementEndPos();

    if (snap.type == SimpleXmlParser::Text) {
      snap.text = readTextForward(parser);
    }

    result.push_back(snap);

    // Show first few nodes for debugging
    if (nodeCount <= 10) {
      std::cout << "    Node " << nodeCount << ": ";
      if (snap.type == SimpleXmlParser::Element) {
        std::cout << "Element <" << snap.name.c_str() << ">";
      } else if (snap.type == SimpleXmlParser::EndElement) {
        std::cout << "EndElement </" << snap.name.c_str() << ">";
      } else if (snap.type == SimpleXmlParser::Text) {
        std::cout << "Text: \"" << snap.text.substring(0, 30).c_str() << (snap.text.length() > 30 ? "..." : "") << "\"";
      }
      std::cout << "\n";
    }
  }

  std::cout << "  Total nodes read: " << nodeCount << "\n";
  std::cout << "  Total bytes streamed: " << callbackCtx.totalBytesRead << " in " << callbackCtx.callCount
            << " calls\n";
  parser.close();

  // Clean up EPUB streaming
  epub_end_streaming(epubStreamCtx);

  return result;
}

void testEpubStreamingParsing(TestUtils::TestRunner& runner, const char* epubPath, int spineIndex) {
  std::cout << "\n=== Testing EPUB Streaming vs File vs Memory Parsing ===\n";
  std::cout << "EPUB: " << epubPath << ", spine index: " << spineIndex << "\n";

  // Step 1: Open EPUB and extract the spine item to a file
  EpubReader reader(epubPath);
  if (!reader.isValid()) {
    std::cout << "ERROR: Failed to open EPUB\n";
    runner.expectTrue(false, "Should be able to open EPUB");
    return;
  }

  const SpineItem* spineItem = reader.getSpineItem(spineIndex);
  if (!spineItem) {
    std::cout << "ERROR: Invalid spine index " << spineIndex << "\n";
    runner.expectTrue(false, "Should have valid spine item");
    return;
  }

  // Build full path relative to content.opf
  String contentOpfPath = reader.getContentOpfPath();
  String baseDir = "";
  int lastSlash = contentOpfPath.lastIndexOf('/');
  if (lastSlash >= 0) {
    baseDir = contentOpfPath.substring(0, lastSlash + 1);
  }
  String fullHref = baseDir + spineItem->href;

  std::cout << "Spine item href: " << spineItem->href.c_str() << "\n";
  std::cout << "Full path in EPUB: " << fullHref.c_str() << "\n";

  // Extract the file to disk
  String extractedPath = reader.getFile(fullHref.c_str());
  if (extractedPath.isEmpty()) {
    std::cout << "ERROR: Failed to extract XHTML from EPUB\n";
    runner.expectTrue(false, "Should be able to extract XHTML from EPUB");
    return;
  }
  std::cout << "Extracted to: " << extractedPath.c_str() << "\n";

  // Step 2: Load extracted file into memory
  File file = SD.open(extractedPath.c_str(), FILE_READ);
  if (!file) {
    std::cout << "ERROR: Cannot open extracted file for reading\n";
    runner.expectTrue(false, "Should be able to open extracted file");
    return;
  }

  size_t fileSize = file.size();
  std::cout << "Extracted file size: " << fileSize << " bytes\n";

  if (fileSize == 0) {
    std::cout << "ERROR: Extracted file is empty\n";
    file.close();
    runner.expectTrue(false, "Extracted file should not be empty");
    return;
  }

  char* buffer = new char[fileSize + 1];
  size_t bytesRead = file.read((uint8_t*)buffer, fileSize);
  buffer[bytesRead] = '\0';
  file.close();

  std::cout << "Bytes read into memory: " << bytesRead << "\n";
  std::cout << "First 200 bytes:\n---\n";
  for (size_t i = 0; i < 200 && i < bytesRead; i++) {
    std::cout << buffer[i];
  }
  std::cout << "\n---\n";

  // Step 3: Parse from extracted file
  std::cout << "\n--- Parsing from FILE (extracted) ---\n";
  auto fileNodes = readForwardNodes(runner, extractedPath.c_str());
  std::cout << "File parsing result: " << fileNodes.size() << " nodes\n";

  // Step 4: Parse from memory
  std::cout << "\n--- Parsing from MEMORY ---\n";
  auto memoryNodes = readForwardNodesFromMemory(runner, buffer, bytesRead);
  std::cout << "Memory parsing result: " << memoryNodes.size() << " nodes\n";

  // Step 5: Parse from EPUB stream (same content, streamed directly)
  std::cout << "\n--- Parsing from EPUB STREAM ---\n";
  auto streamNodes = readForwardNodesFromStream(runner, epubPath, spineIndex);
  std::cout << "Stream parsing result: " << streamNodes.size() << " nodes\n";

  // Cleanup
  delete[] buffer;

  // Compare results
  std::cout << "\n=== COMPARISON (same XHTML content, 3 methods) ===\n";
  std::cout << "File nodes:   " << fileNodes.size() << "\n";
  std::cout << "Memory nodes: " << memoryNodes.size() << "\n";
  std::cout << "Stream nodes: " << streamNodes.size() << "\n";

  bool allMatch = true;

  // Compare file vs memory
  if (fileNodes.size() != memoryNodes.size()) {
    std::cout << "\nERROR: File vs Memory node count mismatch!\n";
    allMatch = false;
    runner.expectTrue(false, "Memory parsing should produce same node count as file parsing");
  } else {
    std::cout << "\n✓ File and Memory produced same node count\n";
    runner.expectTrue(true, "Memory parsing produces correct node count");
  }

  // Compare file vs stream
  if (fileNodes.size() != streamNodes.size()) {
    std::cout << "ERROR: File vs Stream node count mismatch!\n";
    allMatch = false;
    runner.expectTrue(false, "Stream parsing should produce same node count as file parsing");
  } else {
    std::cout << "✓ File and Stream produced same node count\n";
    runner.expectTrue(true, "Stream parsing produces correct node count");
  }

  // Compare memory vs stream
  if (memoryNodes.size() != streamNodes.size()) {
    std::cout << "ERROR: Memory vs Stream node count mismatch!\n";
    allMatch = false;
  } else {
    std::cout << "✓ Memory and Stream produced same node count\n";
  }

  if (allMatch) {
    std::cout << "\n*** ALL THREE METHODS PRODUCED IDENTICAL NODE COUNTS ***\n";

    // Compare first few nodes from all three methods
    int mismatchCount = 0;
    for (size_t i = 0; i < std::min((size_t)10, fileNodes.size()); i++) {
      bool nodeMatches = true;
      if (fileNodes[i].type != memoryNodes[i].type || fileNodes[i].name != memoryNodes[i].name) {
        nodeMatches = false;
      }
      if (fileNodes[i].type != streamNodes[i].type || fileNodes[i].name != streamNodes[i].name) {
        nodeMatches = false;
      }

      if (!nodeMatches) {
        mismatchCount++;
        std::cout << "\n  Node " << i << " mismatch:\n";
        std::cout << "    File:   type=" << (int)fileNodes[i].type << " name=" << fileNodes[i].name.c_str() << "\n";
        std::cout << "    Memory: type=" << (int)memoryNodes[i].type << " name=" << memoryNodes[i].name.c_str() << "\n";
        std::cout << "    Stream: type=" << (int)streamNodes[i].type << " name=" << streamNodes[i].name.c_str() << "\n";
      }
    }

    if (mismatchCount == 0) {
      std::cout << "\n*** SUCCESS: First 10 nodes match perfectly across all methods ***\n";
    } else {
      std::cout << "\nWARNING: " << mismatchCount << " node mismatches in first 10\n";
    }
  }
}

int main() {
  TestUtils::TestRunner runner("SimpleXmlParser Position Test");
  const char* xhtmlPath = TestGlobals::g_testXhtmlPath;

  std::cout << "Test XHTML: " << xhtmlPath << "\n\n";

  // Original file-based test
  std::cout << "=== Testing File-Based Parsing ===\n";
  auto nodes = readForwardNodes(runner, xhtmlPath);
  runner.expectTrue(!nodes.empty(), "Forward pass captured nodes", "", true);
  std::cout << "File parsing: " << nodes.size() << " nodes\n";

  // Test EPUB streaming - extracts XHTML from EPUB and compares all 3 parsing methods
  testEpubStreamingParsing(runner, TestGlobals::g_testFilePath, 1);

  return runner.allPassed() ? 0 : 1;
}
